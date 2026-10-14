# Shape LFO — automated Disting NT builds with GitHub Actions

This project builds using **GNU Arm 14.2.Rel1**, the version that produced a working plugin on the user's Windows machine. It does **not** use the experimental Clang build that failed to load on the Disting NT.

## Set up once

1. Make a new GitHub repository, e.g. `shape-lfo` or `hyper-lfo` (public or private).
2. Unzip this package into the root of the repository. It must contain `shape_lfo.cpp`, `build.sh`, and `.github/workflows/build.yml` at their specified paths. Do not put everything into an extra wrapper folder.
3. Commit and push all files to the repo's `main` branch. The `.github` folder must be committed too (it may be hidden by some file explorers).
4. GitHub runs **Actions → Build Shape LFO for Disting NT** automatically. You can also select **Run workflow** manually on the Actions page.
5. Open the successful run, scroll to **Artifacts**, download the `Shape-LFO-...` artifact ZIP, and extract the `shape_lfo.o` inside. Copy that file into `/programs/plug-ins/` on the Disting NT MicroSD card, replacing any older Shape LFO `.o`. Restart/rescan the module.

## Direct `.o` download for versioned releases

GitHub Actions always wraps normal run artifacts in a ZIP. For *direct* `.o` downloads, create and push a tag matching `v*`, such as `v0.1.2` (for example with GitHub Desktop or Git):

```bash
git tag v0.1.2
git push origin v0.1.2
```

The workflow will attach **shape_lfo.o** to the GitHub **Releases** page without an additional ZIP wrapper. Use a new tag for every later release (e.g. `v0.1.3`); do not move old version tags.

## What happens during a build

- Retrieves the **official** public SDK from `https://github.com/expertsleepersltd/distingNT_API` to `distingNT_API/`.
- Installs **GNU Arm 14.2.Rel1** using the maintained toolchain Action.
- Runs the included host tests.
- Runs `bash build.sh ./distingNT_API` with the same compiler flags used by `build.ps1` on Windows.
- Checks that the result is a nonempty ARM object and inspects unresolved symbols.
- Uploads the compiled `.o` as a GitHub Actions artifact; tags also attach it to a GitHub release.

**Caveat:** We haven't run this workflow on GitHub yet, so the first run is the validation step. The official SDK is currently fetched from its default branch rather than pinned to the exact copy installed on Windows. Once the first GitHub-built `.o` loads successfully, record its SDK commit from the run logs and pin it in the workflow for reproducibility. If it does not load, compare the SDK version with the locally compiled working `.o` before altering any DSP code.

## Continue compiling locally (optional)

The existing local build scripts are still included. On Windows, run `./build.ps1` in PowerShell with the official SDK placed inside or beside the project, or specify `-ApiPath` explicitly.
