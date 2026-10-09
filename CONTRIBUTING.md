# Contributing to KB-RPCS3

Thanks for your interest. KB-RPCS3 is an early, small project, so a little
coordination goes a long way.

## Before you start

- Read the [README](README.md), [LICENSING_STATUS.md](LICENSING_STATUS.md) and
  [BUILDING.md](BUILDING.md). The project is **alpha** and currently ships
  **source only** — there is no downloadable PS5 binary.
- Check [KNOWN_ISSUES.md](KNOWN_ISSUES.md), [COMPATIBILITY.md](COMPATIBILITY.md) and
  [ROADMAP.md](ROADMAP.md) so you are not duplicating known work.
- For anything substantial, open an issue first so the approach can be agreed.

## Ways to contribute

- **Compatibility reports.** Tested a PS3 game on KB-RPCS3? A precise report with
  firmware, loader, settings and what happened is genuinely useful.
- **Testing.** Reproduce known issues on your hardware and add detail.
- **Documentation.** Corrections and clarifications to the docs are welcome.
- **Code.** Bug fixes, port improvements and tooling. Most changes touch the RPCS3
  patch series (`port/rpcs3-patches/`) or the PS5 title shell (`app/`).

## Licensing of contributions

KB-RPCS3 mixes licences, and contributions must not break that:

- Changes to **`port/rpcs3-patches/`** are part of RPCS3 and must be
  **GPL-2.0-only** compatible. Do not introduce code under a licence that is
  incompatible with GPL-2.0.
- Changes to **`app/`** follow the licence of the file you edit (MIT or
  GPL-3.0-or-later — see [`app/LICENSING.md`](app/LICENSING.md)).
- By submitting a contribution you agree it may be distributed under the licence of
  the file it changes.

Do **not** submit:

- PS3/PS5 firmware, games, ROMs, keys, `.pkg`/`.rap` files or any other Sony content.
- Prebuilt binaries of the combined emulator, or anything that would require one to be
  distributed.
- Secrets, tokens or personal data.
- Code you do not have the right to contribute.

## Reporting bugs and requesting features

Use the issue templates for [bug reports](.github/ISSUE_TEMPLATE/bug_report.yml) and
[feature requests](.github/ISSUE_TEMPLATE/feature_request.yml). Security problems go
through [SECURITY.md](SECURITY.md), not public issues.

## Pull requests

- Keep a pull request focused on one change.
- Match the surrounding style; keep existing headers, notices and spelling.
- Describe how the change was tested. Say plainly what was **not** tested.
- Do not add AI assistants as project contributors or co-authors in commits — credit
  belongs to the people who authored the work.

## Commit messages

Short imperative subject, then a body explaining what and why when it is not obvious.
If a change affects the RPCS3 patch series, say which patch and why.

## Conduct

Participation is covered by the [Code of Conduct](CODE_OF_CONDUCT.md).
