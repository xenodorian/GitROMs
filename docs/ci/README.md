# Publishing builds so GitROMs can fetch and update them

GitHub Actions artifacts need a GitHub login to download, so the R36S cannot
fetch them. GitROMs reads the repo's **latest release** instead: open the repo,
then `[Releases]`. Files saved from there are tracked, and the list shows
`UPD` next to a file whose release asset has changed since you saved it.

The two snippets here keep one rolling release named `latest` up to date on
every push to `main` and on manual runs. They are applied on `main` in
xenodorian/SGBA and xenodorian/BibooSGBA and were verified with real CI runs
(see the repos' Releases pages).

Notes:
- The release must NOT be a pre-release. GitHub's `releases/latest` API ignores
  pre-releases and drafts, and GitROMs would see "No release published".
- `--clobber` replaces an asset, which gives it a new id and timestamp. That is
  what GitROMs compares to decide an update exists.
- Anonymous GitHub API calls are limited to 60 per hour. GitROMs caches lists
  per session, so one browse costs a few calls.
