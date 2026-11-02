# Source SBOM

Source repository inventory only; completeness is UNKNOWN. Syft inventories tracked source/package manifests. Gitlinks and declared vendored/model evidence are inspected at immutable pins. CMake declarations are conditional requirements, not resolved installations. This is NOT proof of operating-system packages, static-link closure, build-time resolution, or the contents of release binaries.

Generation: **success**

## Source identity

wave-owl/fldigi-ifk-core `748af7961f86fd2fe39e1838e3ffd55f05a621a1`

- sbom:role: source-root
- sbom:git-commit: 748af7961f86fd2fe39e1838e3ffd55f05a621a1
- sbom:git-tree: 7b639534313f93926af8b90316687dbdcc6bd6f6
- sbom:source-repository: https://github.com/wave-owl/fldigi-ifk-core
- sbom:release-tags: ["v0.1.0"]
- sbom:license-evidence: [{"path": "LICENSE", "sha256": "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"}]
- sbom:license-resolution: unresolved; manual review required

## Generation errors

None.

## Components and unresolved licenses

| Component | Version | Role | License |
| --- | --- | --- | --- |
| wave-owl/fldigi-ifk-core | 748af7961f86fd2fe39e1838e3ffd55f05a621a1 | source-root | unresolved; manual review required |
| wave-owl/fldigi-dsp-core | c6f5a9bc3463db9995e413cafd1aef3198c7cdf6 | gitlink | unresolved; manual review required |
| src/ifk_common.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| src/ifk_tables.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| src/ifk_rx.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| src/ifk_tx.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| actions/checkout | v4 | Syft source/package discovery | unresolved |
| actions/checkout | v4 | Syft source/package discovery | unresolved |
| /Users/gerolfziegenhain/src/wave-owl/local/sbom-core-preflight-v2/fldigi-ifk-core/.source-input/.github/workflows/ci.yml | unresolved | Syft source/package discovery | unresolved |
| /Users/gerolfziegenhain/src/wave-owl/local/sbom-core-preflight-v2/fldigi-ifk-core/.source-input/dsp/.github/workflows/ci.yml | unresolved | Syft source/package discovery | unresolved |

License evidence paths/hashes, source manifests, model hashes and conditional
CMake declarations are recorded in SBOM.cdx.json. Declared upstream pins do not
prove vendored equivalence. No dependency installation or build was performed.
