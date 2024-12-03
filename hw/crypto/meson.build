system_ss.add(when: 'CONFIG_XLNX_VERSAL2_ASU_SHA', if_true: files(
  'xlnx-asu_sha2.c',
  'xlnx-asu_sha3.c',
))

system_ss.add(when: 'CONFIG_XLNX_VERSAL2_ASU_AES', if_true: files(
  'xlnx-asu-aes.c',
  'xlnx-asu-kv.c',
  'xlnx-pmxc-key-transfer.c',
  'xlnx-asu-ecc.c',
))

system_ss.add(when: 'CONFIG_XLNX_SHA3_COMMON', if_true: files(
  'xlnx-sha3-common.c',
))

if gcrypt.found()
  system_ss.add(when: 'CONFIG_XLNX_VERSAL2_ASU_AES', if_true: files(
    'xlnx-asu-aes-gcrypt.c',
  ))
endif
