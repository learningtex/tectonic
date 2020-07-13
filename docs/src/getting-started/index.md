# Getting Started

TODO: installation; quick demonstration of how Tectonic is different from traditional TeX engines.
## Installation

### Fedora 32
- OS info

```
[x220@ipa01 ]$ cat /etc/redhat-release 
Fedora release 32 (Thirty Two)
[x220@ipa01 ]$ 
```

- rust/cargo installation
```
[x220@ipa01 ]$ rustc --version;cargo --version
rustc 1.40.0 (73528e339 2019-12-16)
cargo 1.40.0 (bc8e4c8be 2019-11-22)
[x220@ipa01 ]$ 
```

- Development packages needed for compilation

```
[x220@ipa01 ]$ sudo dnf install -y fontconfig-devel harfbuzz-devel harfbuzz-icu libicu-devel freetype-devel graphite2-devel libpng-devel
[x220@ipa01 ]$ 
```

- Copy existing samplepaper.tex

```
[x220@ipa01 ]$  cp /usr/share/texlive/texmf-dist/doc/xelatex/langsci/examples/samplepaper.tex .
[x220@ipa01 ]$ ls -lrt samplepaper.tex
-rw-r--r-- 1 x220 x220 2940 Jul 13 10:48 samplepaper.tex
[x220@ipa01 ]$

```

- Use tectonic to build samplepaper.tex

```
[x220@ipa01 ]$ tectonic samplepaper.tex
[x220@ipa01 ]$
```