# generated native payload

This directory is populated by `tools/build_native_payload.py`. Generated
binaries and copied experiment assets are ignored by Git. Run the preparation
tool before asking Booster Studio to build the `.agent` package.

The generated layout is:

```text
payload/
├── manifest.json
├── common/
│   ├── configs/
│   ├── evidence/manifests/
│   └── lisp/
└── sim_x86_64/bin/humanoid_model_mediated_trial
```

`manifest.json` binds every packaged file to a SHA-256 digest and the source Git
commit. The package verifier rejects a missing, changed or wrong-architecture
runner.

Build and verify the payload from the repository root:

```bash
python3 examples/humanoid_model_mediated_approach/booster_studio/tools/build_native_payload.py
python3 examples/humanoid_model_mediated_approach/booster_studio/tools/build_native_payload.py \
  --check-only
```

`--source-check` validates the build definition and frozen inputs without a
Docker daemon. `--binary PATH` is intended for a trusted external Linux build;
the same ELF64 x86-64 and digest checks still apply.

The default release build refuses relevant source-tree changes. Use
`--allow-dirty` only for a development package; the manifest records the dirty
state and a digest of the Git status.

Temporary build output may be on a different filesystem from the repository.
Publication copies that output into a temporary sibling of this directory
before replacing the managed payload entries.
