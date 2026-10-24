"""ORT ile bütün ara çıktıları dök (sınama referansı)."""
import sys, numpy as np, onnx, onnxruntime as ort
from onnx import helper, shape_inference
from piper import PiperVoice
def dok(onnx_yol, metin, cikti):
    v=PiperVoice.load(onnx_yol.replace('.onnx','.onnx'))
    ids=[]
    for s in v.phonemize(metin): ids+=v.phonemes_to_ids(s)
    m=onnx.load(onnx_yol)
    m=shape_inference.infer_shapes(m)
    tip={vi.name:vi.type.tensor_type.elem_type for vi in list(m.graph.value_info)}
    var=set(o.name for o in m.graph.output)
    for n in m.graph.node:
        if n.op_type=='Constant': continue
        for o in n.output:
            if o and o not in var and o in tip and tip[o]!=0:
                m.graph.output.append(helper.make_tensor_value_info(o, tip[o], None)); var.add(o)
    s=ort.InferenceSession(m.SerializeToString(), providers=['CPUExecutionProvider'])
    adlar=[o.name for o in s.get_outputs()]
    inp={"input":np.array([ids],np.int64),"input_lengths":np.array([len(ids)],np.int64),"scales":np.array([0.667,1.0,0.8],np.float32)}
    out=s.run(None,inp)
    d={k:v for k,v in zip(adlar,out)}
    d['__ids']=np.array(ids,np.int64)
    np.savez(cikti, **{k.replace('/','|'):v for k,v in d.items()})
    print("dokulen", len(d), "ses", d['output'].shape)
if __name__=='__main__': dok(sys.argv[1], sys.argv[2], sys.argv[3])
