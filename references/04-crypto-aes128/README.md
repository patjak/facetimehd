# Reference: NIST FIPS-197 AES-128 Cryptographic Standard

## 1. Provenance & Origin
* **Specification**: Federal Information Processing Standards Publication 197 (FIPS-197)
* **Publisher**: National Institute of Standards and Technology (NIST), U.S. Department of Commerce
* **URL**: [https://csrc.nist.gov/publications/detail/fips/197/final](https://csrc.nist.gov/publications/detail/fips/197/final)
* **Block Chaining Mode**: NIST Special Publication 800-38A (CBC Mode)
* **License / Status**: Public Domain / U.S. Government Work

## 2. Technical Purpose & Usage in OpenFTHD
Documents the standard S-Box substitution tables, Galois Field multiplication polynomials ($GF(2^8)$ with irreducible polynomial $m(x) = x^8 + x^4 + x^3 + x + 1$), round key expansion algorithms, and official Appendix C.1 test vectors implemented in `src/aes.c`.
