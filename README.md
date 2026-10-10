# SMC_NPK_Tool
Command line tool to extract resources from Super Mecha Champions

- depends on lz4 vcpkg

Create an uncompressed NPK from a directory:

```text
SMCNPK.exe -c <input_dir> <output.npk>
```

Created entries use compression type 3 with no payload encryption; the archive header type is 3.

To make `-p` produce an exact total archive size, pass the target size in bytes as the final argument:

```text
SMCNPK.exe -p <input.npk> <input_dir> <output.npk> <target-size-bytes>
```

If the payloads do not fit before the index, patching fails instead of truncating data. Padding is written before the encrypted index.

## Build a standalone Windows patcher

The executable also has a graphical mode: double-click `SMCNPK.exe` and select
the game folder in the folder picker. The default Steam installation path is
preselected. While archives are being processed, a separate window displays
an animated progress indicator.
It patches `package/res.npk`, `package/res3.npk`, and
`package/patch_npk/p_res_1.npk`; the latter is rebuilt to exactly 87,402,532
bytes. The patch resources and their originals are embedded in the executable;
`org_flist.lst` is only used by unpacking and is not needed to patch.

Before building, `Release/res` must contain the replacement files and
`Release/original` must contain the original versions at exactly the same
relative paths. For example, a file at `Release/res/confs/text/en/example.json`
must have its original counterpart at
`Release/original/confs/text/en/example.json`. The build stops if either set
is empty or the paths do not match.

The version in `patch-version.txt` is embedded in the executable. Increase it
before building a new patch release. Running the same version again restores
the embedded originals and removes `.smc_npk_patch_version` from the game
folder. A different installed version is updated in place without restoring
the originals first. `build.sh` uses MinGW-w64 and the static vcpkg LZ4 library
configured in the script; the resulting `Release/SMCNPK.exe` includes both
the patch data and its runtime dependencies.
