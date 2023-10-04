system_ss.add(when: 'CONFIG_XLNX_VERSAL2_ASU_SHA', if_true: files(
  'xlnx-asu_sha2.c',
  'xlnx-asu_sha3.c',
  'xlnx-pmc-sha3-v2.c',
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

system_ss.add(when: 'CONFIG_XLNX_SPARTANUP_PMC_SHA3', if_true: files(
  'xlnx-spartanup-pmc_sha3.c',
))
