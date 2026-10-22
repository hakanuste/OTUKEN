# BearSSL 0.6 (üçüncü taraf, MIT)

TULPAR'ın HTTPS katmanı (`userprog/tls.c`) bu kitaplığı kullanır. Kriptografi
kendimiz yazılmadı: kendi yazılmış TLS kriptosu güvenlik açığı kaynağıdır.

- Kaynak: Ubuntu arşivi `pool/universe/b/bearssl/bearssl_0.6+dfsg.1.orig.tar.xz`
  (sha256 102a625bd8f6155260867a50c508df8d8c3f796985dd744df1a68e5cd1a79004).
  BearSSL'in son sürümü 0.6'dır (2018). TLS 1.0–1.2; TLS 1.3 yoktur.
- Yalnız `src/`, `inc/`, `LICENSE.txt`, `README.txt` alındı; kod DEĞİŞTİRİLMEDİ.
- Derleme (Makefile, `userprog/bearssl.a`): Ring3 bayraklarıyla (SSE yok,
  -mcmodel=large), `BR_USE_URANDOM=0 BR_USE_UNIX_TIME=0 BR_RDRAND=0
  BR_AES_X86NI=0 BR_SSE2=0 BR_POWER8=0 BR_INT128=1`: yalnız sabit zamanlı
  (ct) taşınabilir uygulamalar. Rastgelelik ve saat çekirdekten gelir
  (SYS_RASTGELE: RDSEED/RDRAND, SYS_ZAMAN: CMOS RTC).
- Güven kökleri: `userprog/tls_kokler.c` (`araclar/kok_sertifika_uret.py
  /usr/share/ca-certificates/mozilla`: Ubuntu ca-certificates 20240203 =
  Mozilla listesi, 146 kök). Üretilen diziler BearSSL'in `brssl ta` aracının
  çıktısıyla bayt bayt aynıdır (denetlendi). `/etc/ssl/certs` demeti
  KULLANILMAZ: yerel olarak eklenmiş (vekil sunucu) kökleri de içerir; ilk
  üretimde bu yüzden 7 yabancı kök girmişti, fark edilip çıkarıldı.
