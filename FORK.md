# CrossPaper Fork Guide

CrossPaper is a long-lived fork of [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk). It adds Instapaper
sync and its own branding, and it takes regular merges from upstream. It is not intended to be merged back upstream.

**Keep the diff against upstream as small as possible.** Every changed line is a line that can conflict on the next
sync. Prefer new files over edits to upstream files, and small, surgical edits over rewrites.

## What diverges from upstream

| Area | Change | Where |
| --- | --- | --- |
| Instapaper sync | New feature: web portal page, device relay API, Articles screen | `lib/InstapaperSync/`, `src/network/InstapaperWebApi.*`, `web/pages/instapaper.*`, related activity changes |
| OTA updates | Devices check CrossPaper releases, not CrossInk's | `src/network/OtaUpdater.cpp` (the `CROSSINK_OTA_RELEASE_URL` default) |
| Release workflow | Catalog generation and the signed `docs/catalog` push were removed. The token falls back to `github.token` | `.github/workflows/release.yml` |
| RC workflow | The token falls back to `github.token` | `.github/workflows/release_candidate.yml` |
| Removed workflows | `pages.yml` (crossink.dev site), `issue-triage.yml` and `.github/aw/` (upstream triage bot), `release-fonts.yml` (publishes to upstream's font repo and S3) | deleted |
| Branding | CrossPaper name and assets (in progress) | TBD |

What stays pointed at upstream, on purpose:

- **SD card fonts.** Devices download fonts from upstream's `crossink-fonts` bucket. The URL is keyed by the font
  format versions (`FONTS_MANIFEST_VERSION` and `CPFONT_VERSION`), so it stays compatible as long as this fork tracks
  upstream.
- **Simulator** (`uxjulia/crossink-simulator`) and the **`freeink-sdk`** submodule.
- **Upstream docs and site** (`docs/`, `site/`, `docs/CNAME`) stay in the tree unchanged. Nothing deploys them.
  Put CrossPaper's own docs site in a separately named folder with its own workflow, so it never collides with
  upstream's.

## Do not touch

These look like they need fork edits, but changing them costs a conflict on every sync or breaks OTA safety:

- `docs/catalog`: upstream rewrites it on every release.
- `[crossink] version` in `platformio.ini`: leave it at upstream's value. The release version comes from the workflow
  input.
- The board tag magic `CROSSPOINT-BOARD-V1` in `src/network/FirmwareBoardTag.cpp`. It stops a device from flashing
  firmware built for another board.
- Firmware asset names (`firmware-<device>-v<version>.bin`). The OTA updater matches them.
- The `/.crosspoint` on-device paths (caches, OTA staging).

## Versioning

CrossPaper versions are **`<upstream version>.<N>`**:

- `v1.6.0.1`, `v1.6.0.2`, … for CrossPaper releases based on upstream 1.6.0.
- `v1.7.0.1` for the first CrossPaper release after merging upstream 1.7.0.

Always use four dotted numbers. The OTA updater compares up to four numeric segments and ignores everything after a
`-`, so a `v1.6.0-2` release would compare equal to `v1.6.0-1` and never be offered.

## Cutting a release

1. Make sure `main` (or whichever branch you release from) is green in CI and `CHANGELOG.md` has a
   `## [v<version>]` entry.
2. In GitHub, go to **Actions → Compile Release → Run workflow**, pick the branch, and enter the version without the
   `v` (for example `1.6.0.2`).
3. The workflow builds X3/X4, Sticky, X4 Pro, and X4 Classic firmware and creates a **draft** release `v<version>`
   at the commit it ran on.
4. Review the draft, paste in the changelog notes, and click **Publish**.

Devices see nothing until the release is published. The updater reads `releases/latest`, which ignores drafts and
prereleases. GitHub attaches a SHA-256 `digest` to each asset automatically, and the device checks the downloaded
firmware against it before flashing.

Check a published release:

```sh
gh api repos/sherloaf-holmes/CrossPaper/releases/latest --jq '.tag_name, (.assets[] | .name + " " + .digest)'
```

No secrets are required. If a `RELEASE_PAT` repository secret exists, the workflows use it instead of `github.token`.

### Release candidates

Push a branch named `release/<name>`, then run **Actions → Compile Release Candidate** on that branch. It creates a
draft *prerelease* tagged `rc-<name>-<hash>`. Prereleases are never offered over OTA, so use them to flash test
devices by hand.

## Syncing with upstream

1. Use **Sync fork** on the repository page. If it reports conflicts, open a PR from `uxjulia/CrossInk:main` into
   `sherloaf-holmes/CrossPaper:main` instead.
2. **Merge with a merge commit. Never squash or rebase an upstream sync.** A squash drops the merge base, so the next
   sync re-conflicts on everything.
3. Let CI run on the sync PR. It builds `default` and `x4-pro` and runs clang-format and cppcheck.

Resolve harder conflicts locally:

```sh
git remote add upstream https://github.com/uxjulia/CrossInk.git   # once
git fetch upstream
git switch main && git pull
git merge upstream/main
```

Expected conflict hot spots:

- `CHANGELOG.md`: keep both. CrossPaper entries go above the upstream version they're based on.
- The OTA URL line in `src/network/OtaUpdater.cpp`: keep the CrossPaper URL.
- The token lines and the removed catalog steps in `release.yml`: keep the fork versions, but take upstream's other
  changes (new build matrix entries, action version bumps).
- **Modify/delete conflicts** on the removed workflows (`pages.yml`, `issue-triage.yml`, `release-fonts.yml`,
  `.github/aw/`): GitHub's web editor can't resolve these. Run `git rm <file>` locally, then commit the merge.

After a sync that bumps upstream's version (for example to 1.7.0), the next CrossPaper release is `1.7.0.1`.

## One-time repository setup

- Enable Actions on the fork: **Settings → Actions → General → Allow all actions**. GitHub disables Actions on forks
  by default.
- GitHub Pages stays off until CrossPaper has its own docs site.

## Moving from pre-fork builds

Firmware built before the OTA URL change still checks CrossInk's releases, and will update itself to CrossInk as soon
as upstream publishes a newer version. Reflash such devices once over USB with a CrossPaper release. From then on,
updates come from CrossPaper releases.
