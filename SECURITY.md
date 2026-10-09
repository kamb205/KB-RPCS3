# Security policy

## Scope

KB-RPCS3 is a source release. It contains the RPCS3 patch series, the PS5 title shell
and helper scripts. The most relevant security topics are:

- Destructive or unsafe behaviour in the project's own scripts and tools.
- Build-system issues that could execute untrusted input.
- Code in the PS5 title shell that mishandles untrusted files (firmware, packages,
  game images) in a way that could damage a console or a user's data.

Found a problem in **upstream RPCS3** or **the Mesa/RADV driver**? Report it to those
projects instead.

## Supported versions

Only the latest alpha is considered. There is no long-term support: this is an
alpha-stage, unofficial project and fixes are best-effort.

| Version | Supported |
|---|---|
| Alpha 0.1.0 | Best effort |
| Older | No |

## Reporting a vulnerability

**Do not open a public issue for a security problem.**

Use GitHub's private reporting instead: open the repository's **Security** tab and
choose **Report a vulnerability**. That keeps the report private until a fix is ready.
If that is unavailable to you, contact the maintainer, **KB**, through GitHub and ask
for a private channel — do not post technical details in a public thread.

Please include:

- what the issue is and where it lives (file, script, or build step);
- how to reproduce it, and the console/firmware/loader involved if relevant;
- the impact you believe it has.

You will get an acknowledgement when the report is seen. Please give the maintainer
reasonable time to respond before disclosing anything publicly.
