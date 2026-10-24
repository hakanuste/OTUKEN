"""OSES motorunu ORT referansıyla düğüm düğüm karşılaştırır.
# derleme: gcc -O3 -I.. -o sina_ana sina_ana.c ../oses.c ../oses_mat.c
  karsilastir.py model.oses ref.npz
Rastgele gürültü ORT'nin döktüğünden verilir; ilk ıraksayan düğüm yazılır."""
import sys, struct, subprocess, numpy as np, os
def main(oses, ref, tolerans=2e-3):
    adlar=open(oses+'.adlar').read().split('\n')
    r=np.load(ref); R={k.replace('|','/'):r[k] for k in r.files}
    d=os.path.dirname(ref) or '.'
    ids=R['__ids'].astype(np.int64); ids.tofile(d+'/ids.bin')
    g=bytearray()
    for ad in ('/dp/RandomNormalLike_output_0','/RandomNormalLike_output_0'):
        a=R[ad].astype(np.float32); i=adlar.index(ad)
        g+=struct.pack('<II',i,a.size)+a.tobytes()+struct.pack('<B',a.ndim)+struct.pack(f'<{a.ndim}q',*a.shape)
    open(d+'/gurultu.bin','wb').write(g)
    p=subprocess.run([os.path.dirname(os.path.abspath(__file__))+'/sina_ana',oses,d+'/ids.bin',d+'/gurultu.bin',d+'/dok.bin'],capture_output=True)
    print(p.stderr.decode().strip())
    ses=np.frombuffer(p.stdout,np.float32)
    b=open(d+'/dok.bin','rb').read(); o=0; say=0; kotu=0; ilk=None
    while o<len(b):
        tid,dt,nd=struct.unpack_from('<iBB',b,o); o+=6
        dims=struct.unpack_from(f'<{nd}q',b,o); o+=8*nd
        s,sa,mx=struct.unpack_from('<ddd',b,o); o+=24
        tam=b[o]; o+=1
        n=int(np.prod(dims)) if nd else 1
        veri=None
        if tam:
            es={1:4,2:8,3:1}[dt]; veri=np.frombuffer(b,{1:np.float32,2:np.int64,3:np.uint8}[dt],n,o); o+=es*n
        ad=adlar[tid]
        if ad not in R: continue
        rv=R[ad]; say+=1
        if tuple(rv.shape)!=tuple(dims):
            kotu+=1; ilk=ilk or (ad,'BICIM',rv.shape,dims); continue
        rf=rv.astype(np.float64).reshape(-1)
        olc=max(np.abs(rf).max() if rf.size else 0,1e-6)
        if veri is not None:
            e=np.abs(veri.astype(np.float64)-rf).max() if rf.size else 0
        else:
            e=max(abs(sa-np.abs(rf).sum())/max(np.abs(rf).sum(),1e-6)*olc, abs(mx-np.abs(rf).max()))
        if e/olc>tolerans:
            kotu+=1
            if ilk is None: ilk=(ad,'DEGER',float(e),float(olc))
    print(f"karsilastirilan {say} kotu {kotu} ilk {ilk}")
    ro=R['output'].reshape(-1)
    if len(ses)==len(ro):
        print("cikis en buyuk fark", float(np.abs(ses-ro).max()), "olcek", float(np.abs(ro).max()))
    else: print("cikis uzunlugu farkli", len(ses), len(ro))
if __name__=='__main__': main(sys.argv[1], sys.argv[2])
