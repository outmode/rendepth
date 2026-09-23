# Rendepth licensing documents

The repository root `LICENSE` remains the MIT grant for original Rendepth code.
`Legal/RENDEPTH_APP_LICENSE` explains that grant alongside the GPL-3.0-only terms
for the combined application and reproduces both license texts.
`Legal/THIRD_PARTY_LICENSING` compiles the component notices into one text file.
Upstream component licenses are preserved; they are not rewritten as GPL.

Regenerate the two documents from the initialized, pinned dependency tree:

```sh
python3 Tools/update_legal_notices.py
python3 Tools/update_legal_notices.py --check
python3 Tools/test_update_legal_notices.py
```

`manifest.json` identifies included notices and component scope. Text is taken
directly from source files, with hashes recording their content. Single-header
notices are extracted from their license comments. Do not hand-edit generated
files; update the introductions, manifest, or notice inputs and regenerate.

`inputs/` contains snapshots for dependencies not vendored in this repository:
Linux system-package notices (versions are recorded in the manifest), the Lato
OFL notice matching the bundled font's metadata, and ONNX Runtime 1.22.0 CPU SDK
notices. The Noto OFL notice is already checked in with the browser extension.
Update snapshots, attributions, and font version labels when replacing those
inputs. Font hashes in the generated compilation identify the covered files.

## Packaging

`Packaging/Legal.cmake` writes build-specific documents to `generated/Legal`.
It replaces the reference ONNX section with the complete LICENSE and
ThirdPartyNotices.txt from the configured CPU SDK, failing if either is missing.
This prevents a runtime update from shipping the previous SDK's notices.
No SDK filesystem path is written into the installed document.

Both documents are installed in `libexec/rendepth/Legal`, alongside the app's
`Binary`, `Assets`, and `Library` directories. Copies are also retained in
`share/licenses/rendepth` for system package conventions. The macOS bundle path
also includes them in its `Legal` directory. Installer license text and RPM
application-license metadata use GPLv3. Use a fresh package staging directory;
an install over an old tree does not delete old notice files.

## Release review

- Source need not be bundled inside the app installer. For downloadable releases,
  publish the matching source separately and place clear, no-extra-charge source
  download directions beside the binary download (GPLv3 section 6(d)). A GitHub
  release asset containing the complete source, recursive dependencies, patches,
  and build scripts is one practical option. A tag plus accessible recursive
  submodules is useful too, provided it supplies the complete corresponding tree.
  Automatic GitHub source ZIPs omit submodule contents.
- The manifest's `source_urls`, `source_revision`, and `revision_url` fields provide
  dependency reference links. Refresh revisions with dependency updates; publish
  local modifications separately. Upstream links do not certify that a release's
  actual source has been published. Do not put credentials or private repository
  URLs in the generated documents.
- Check the actual dependency inventory for each platform and build. Update the
  manifest for new dependencies, nested libraries, and build options. Optional
  entries do not mean all of those libraries are shipped or all license options
  are selected.
- GPL binaries need matching, complete Corresponding Source and build/install
  instructions under GPLv3 section 6. Preserve the exact submodule revisions,
  local dependency changes, and any static-library sources. The current private
  RapidJSON submodule URL is not by itself public source access: arrange access
  to the covered source for recipients. A notice compilation or the repository's
  MIT license does not replace these distribution obligations.
- Retain original notices in source distributions too. The generator compiles
  notices; it does not decide whether a particular dependency combination is
  redistributable or replace a review of applicable terms.
- Optional CUDA, cuDNN, DirectML, and ROCm packs keep their exact vendor notices
  in their own `licenses` directories. Evaluate those builds separately; the
  base application's GPL declaration does not grant rights to proprietary SDKs.
- Downloaded model weights require their own matching licenses and attribution.
  They are not included in this base-application notice inventory.

References: [GNU GPLv3](https://www.gnu.org/licenses/gpl-3.0.html),
[GNU licensing FAQ](https://www.gnu.org/licenses/gpl-faq.html),
[GNU license compatibility list](https://www.gnu.org/licenses/license-list.html).
