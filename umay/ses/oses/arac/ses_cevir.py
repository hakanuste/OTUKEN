"""ONNX (Piper/VITS) -> OSES: ÖTÜKEN ses motorunun okuduğu yalın çizge biçimi.

Biçim (küçük uçlu):
  baslik : "OSES" u32 surum, u32 tensor_sayisi, u32 dugum_sayisi,
           u32 giris_sayisi, u32 cikis_sayisi, u32 veri_ofset
  girisler: i32[giris_sayisi]   (input, input_lengths, scales)
  cikislar: i32[cikis_sayisi]
  tensorler (her biri 64 bayt):
           u8 tur (0 ara, 1 sabit), u8 dtype (1 f32, 2 i64, 3 bool, 4 q8),
           u8 ndim, u8 b, u32 b2, i64 dims[6]? -> i32 dims[6], u64 veri_ofset, u64 veri_bayt, u32 olcek_ofset(q8)
  dugumler: u16 op, u8 nin, u8 nout, i32 in[nin], i32 out[nout],
            i64 ia[16], f32 fa[4]
  veri    : 16 bayt hizalı ham veri

Constant düğümleri sabit tensöre çevrilir (çizgeden çıkar). int32 sabitler
int64 yapılır. RandomNormalLike düğümleri kalır (motor kendi üretecini
kullanır; sınamada dışarıdan verilir).
"""
import sys, struct, numpy as np, onnx
from onnx import numpy_helper, helper

OPLAR = ["Unsqueeze", "Shape", "Mul", "Add", "Slice", "Reshape", "Concat", "Gather", "Transpose",
         "ConstantOfShape", "Sub", "Cast", "Conv", "Expand", "Div", "ReduceMean", "Where", "Equal",
         "Pad", "Range", "Pow", "Sqrt", "ScatterND", "MatMul", "Erf", "NonZero", "GatherElements",
         "Tanh", "Sigmoid", "LeakyRelu", "GatherND", "Squeeze", "Softmax", "Neg", "Split", "CumSum",
         "Exp", "Relu", "GreaterOrEqual", "ReduceSum", "Less", "LessOrEqual", "And", "Not",
         "Softplus", "ConvTranspose", "RandomNormalLike", "Ceil", "Clip", "ReduceMax"]
OPKOD = {o: i for i, o in enumerate(OPLAR)}
DT = {1: 1, 7: 2, 9: 3, 6: 2}       # onnx -> oses (int32 -> int64)
NPDT = {1: np.float32, 2: np.int64, 3: np.bool_}

def ozellik(n):
    a = {x.name: helper.get_attribute_value(x) for x in n.attribute}
    ia = [0] * 16; fa = [0.0] * 4
    op = n.op_type
    if op in ("Gather", "GatherElements", "Concat", "Softmax", "Split"):
        ia[0] = a.get("axis", 0 if op != "Softmax" else -1)
    elif op == "Transpose":
        p = list(a["perm"]); ia[0] = len(p); ia[1:1 + len(p)] = p
    elif op == "Cast":
        ia[0] = DT[a["to"]]
    elif op in ("Conv", "ConvTranspose"):
        assert len(a["kernel_shape"]) == 1, n.name
        ia[0] = a.get("dilations", [1])[0]; ia[1] = a.get("group", 1); ia[2] = a["kernel_shape"][0]
        p = a.get("pads", [0, 0]); ia[3] = p[0]; ia[4] = p[1]; ia[5] = a.get("strides", [1])[0]
        ia[6] = a.get("output_padding", [0])[0] if op == "ConvTranspose" else 0
    elif op == "Reshape":
        ia[0] = a.get("allowzero", 0)
    elif op == "ConstantOfShape":
        t = numpy_helper.to_array(a["value"]) if "value" in a else np.zeros(1, np.float32)
        ia[0] = DT[a["value"].data_type] if "value" in a else 1
        v = t.reshape(-1)[0]
        if ia[0] == 1: fa[0] = float(v)
        else: ia[1] = int(v)
    elif op == "Pad":
        assert a.get("mode", b"constant") == b"constant"
    elif op == "ReduceMean":
        ax = list(a["axes"]); ia[0] = len(ax); ia[1:1 + len(ax)] = ax; ia[15] = a.get("keepdims", 1)
    elif op in ("ReduceSum", "ReduceMax"):
        ia[15] = a.get("keepdims", 1)
    elif op == "LeakyRelu":
        fa[0] = a.get("alpha", 0.01)
    elif op == "RandomNormalLike":
        fa[0] = a.get("mean", 0.0); fa[1] = a.get("scale", 1.0); fa[2] = a.get("seed", 0.0)
    elif op == "CumSum":
        ia[0] = a.get("exclusive", 0); ia[1] = a.get("reverse", 0)
    return ia, fa

def cevir(onnx_yol, cikti, sinama_gurultu=False):
    m = onnx.load(onnx_yol)
    g = m.graph
    adlar = {}
    tensor = []          # (ad, tur, dtype, dims, veri bytes)
    def kimlik(ad):
        if ad == "": return -1
        if ad not in adlar:
            adlar[ad] = len(tensor); tensor.append([ad, 0, 0, [], b""])
        return adlar[ad]
    def sabit(ad, arr):
        i = kimlik(ad)
        if arr.dtype == np.int32: arr = arr.astype(np.int64)
        dt = {np.dtype(np.float32): 1, np.dtype(np.int64): 2, np.dtype(np.bool_): 3}[arr.dtype]
        tensor[i][1:] = [1, dt, list(arr.shape), np.ascontiguousarray(arr).tobytes()]
    for ini in g.initializer:
        sabit(ini.name, numpy_helper.to_array(ini))
    girisler = [kimlik(i.name) for i in g.input]
    dugum = []
    gurultu_adlari = []
    for n in g.node:
        if n.op_type == "Constant":
            a = {x.name: x for x in n.attribute}
            sabit(n.output[0], numpy_helper.to_array(a["value"].t)); continue
        assert n.op_type in OPKOD, n.op_type
        if n.op_type == "RandomNormalLike": gurultu_adlari.append(n.output[0])
        ia, fa = ozellik(n)
        dugum.append((OPKOD[n.op_type], [kimlik(x) for x in n.input], [kimlik(x) for x in n.output], ia, fa))
    cikislar = [kimlik(o.name) for o in g.output]
    # ikili yaz
    bas = bytearray()
    bas += b"OSES" + struct.pack("<IIIIII", 1, len(tensor), len(dugum), len(girisler), len(cikislar), 0)
    bas += struct.pack(f"<{len(girisler)}i", *girisler)
    bas += struct.pack(f"<{len(cikislar)}i", *cikislar)
    veri = bytearray()
    tkayit = bytearray()
    for ad, tur, dt, dims, b in tensor:
        ofs = 0
        if tur == 1:
            while len(veri) % 16: veri += b"\0"
            ofs = len(veri); veri += b
        d6 = (list(dims) + [0] * 6)[:6]
        assert len(dims) <= 6
        tkayit += struct.pack("<BBBBI6iQQ", tur, dt, len(dims), 0, 0, *d6, ofs, len(b))
    dkayit = bytearray()
    for op, ins, outs, ia, fa in dugum:
        dkayit += struct.pack("<HBB", op, len(ins), len(outs))
        dkayit += struct.pack(f"<{len(ins)}i", *ins) + struct.pack(f"<{len(outs)}i", *outs)
        dkayit += struct.pack("<16q4f", *ia, *fa)
    govde = bas + tkayit + dkayit
    while len(govde) % 16: govde += b"\0"
    veri_ofset = len(govde)
    govde[24:28] = struct.pack("<I", veri_ofset)
    open(cikti, "wb").write(bytes(govde) + bytes(veri))
    # tanılama: ad tablosu (sınama için)
    with open(cikti + ".adlar", "w") as f:
        for ad, *_ in tensor: f.write(ad + "\n")
    print(f"tensor {len(tensor)} dugum {len(dugum)} veri {len(veri)/1e6:.1f} MB gurultu {gurultu_adlari}")

if __name__ == "__main__":
    cevir(sys.argv[1], sys.argv[2])
