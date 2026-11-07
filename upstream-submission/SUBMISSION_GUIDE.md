# Guia de Submissão Oficial para o Kernel Linux (Staging & linux-firmware)

Este guia orienta a submissão formal do driver `facetimehd` (`bcwc_pcie`) e do firmware clean-room `OpenFTHD` para a árvore oficial do Kernel Linux (**`drivers/staging/media/facetimehd/`**) e para o repositório **`linux-firmware.git`**.

---

## 1. Estrutura do Pacote de Submissão

O diretório `upstream-submission/` contém dois pacotes completos e prontos para envio:

```text
upstream-submission/
├── kernel-patches/
│   ├── 0000-cover-letter.patch       # Carta de apresentação técnica para os mantenedores
│   ├── 0001-staging-media-facetimehd-add-driver-for-Apple-FaceTi.patch  # Código do driver
│   └── 0002-staging-media-wire-up-facetimehd-in-Kconfig-and-Make.patch  # Integração Kconfig/Makefile
├── linux-firmware-patch/
│   ├── 0001-linux-firmware-add-OpenFTHD-firmware-for-Apple-FaceT.patch  # Patch com WHENCE
│   ├── WHENCE.entry                  # Registro de licença GPLv2 e proveniência
│   └── facetimehd/
│       ├── firmware.bin              # Binário clean-room em C (1.7 KB)
│       └── firmware_rust.bin         # Binário bare-metal em Rust (420 bytes)
└── SUBMISSION_GUIDE.md               # Este guia prático
```

---

## 2. Passo 1: Submissão do Firmware para o `linux-firmware.git`

Antes ou em paralelo à aceitação do driver no kernel, o firmware deve ser submetido à lista oficial de firmwares do kernel:

### Destinatários:
- **Lista:** `linux-firmware@kernel.org`
- **Mantenedores:** Josh Boyer (`jwboyer@kernel.org`) / Kyle McMartin (`kyle@kernel.org`)

### Comando de envio via `git send-email`:
```bash
git send-email \
  --to="linux-firmware@kernel.org" \
  --cc="carlosalberto4ti@gmail.com" \
  upstream-submission/linux-firmware-patch/0001-linux-firmware-add-OpenFTHD-firmware-for-Apple-FaceT.patch
```

---

## 3. Passo 2: Submissão do Driver para a Árvore de Staging (`drivers/staging/`)

O subsistema de Staging do kernel Linux, liderado por **Greg Kroah-Hartman**, é a porta de entrada recomendada pela comunidade para drivers de engenharia reversa que funcionam no hardware real mas ainda necessitam de limpezas pontuais de estilo de código (`checkpatch.pl`) ao longo dos ciclos de release.

### Destinatários Oficiais:
- **Mantenedor de Staging:** Greg Kroah-Hartman (`gregkh@linuxfoundation.org`)
- **Mantenedor do Media Subsystem:** Mauro Carvalho Chehab (`mchehab@kernel.org`)
- **Mantenedor do V4L2:** Hans Verkuil (`hverkuil@xs4all.nl`)
- **Listas de Discussão Públicas:**
  - `linux-media@vger.kernel.org`
  - `devel@driverdev.osuosl.org`
  - `linux-kernel@vger.kernel.org`
- **Autor Original do Driver:** Patrik Jakobsson (`patrik.r.jakobsson@gmail.com`)

### Comando de envio via `git send-email`:
```bash
git send-email \
  --to="gregkh@linuxfoundation.org" \
  --cc="mchehab@kernel.org" \
  --cc="hverkuil@xs4all.nl" \
  --cc="patrik.r.jakobsson@gmail.com" \
  --cc="linux-media@vger.kernel.org" \
  --cc="devel@driverdev.osuosl.org" \
  --cc="linux-kernel@vger.kernel.org" \
  upstream-submission/kernel-patches/0000-cover-letter.patch \
  upstream-submission/kernel-patches/0001-staging-media-facetimehd-add-driver-for-Apple-FaceTi.patch \
  upstream-submission/kernel-patches/0002-staging-media-wire-up-facetimehd-in-Kconfig-and-Make.patch
```

---

## 4. Configuração Prévia do `git send-email`

Se você ainda não tiver configurado o SMTP no Git local, adicione em `~/.gitconfig`:
```ini
[sendemail]
    smtpServer = smtp.gmail.com
    smtpServerPort = 587
    smtpEncryption = tls
    smtpUser = carlosalberto4ti@gmail.com
    # Utilizar uma 'App Password' gerada na Conta Google (Segurança -> Senhas de App)
```

---

## 5. Como Funciona o Ciclo de Revisão Upstream

1. **Recepção:** O patch entra no patchwork do kernel (`patchwork.linuxtv.org` e `lore.kernel.org/linux-media`).
2. **Revisão Técnica:** Desenvolvedores do V4L2 (como Hans Verkuil) e Greg KH podem sugerir pequenos ajustes.
3. **Árvore `staging-testing`:** Greg KH aplica o patchset na árvore `staging.git` (branch `staging-testing`).
4. **Merge no `linux-next`:** O código é integrado à árvore de testes diários do kernel.
5. **Merge Window:** Na abertura da janela de merge seguinte (ex: Linux 7.1 ou 7.2), Linus Torvalds puxa a árvore de staging para o repositório principal `torvalds/linux.git`.
