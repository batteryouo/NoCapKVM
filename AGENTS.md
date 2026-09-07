# Project Instructions

Read and follow these project documents before making changes:
- `docs/coding-standards.md`
- `docs/project-brief.md`

These documents are mandatory project context. If a referenced document is missing, do not invent its contents; continue only with the instructions that are actually available.

## Repository Language

All repository content must be in English, including code comments, identifiers, docstrings, commit messages, documentation, and notes. User-facing UI strings may be localized when appropriate.

## Change Discipline

- Inspect the relevant code and tests before modifying behavior.
- Keep changes focused on the requested task.
- Do not introduce unrelated refactors.
- Follow the existing architecture unless the task explicitly requires an architectural change.
- Run the relevant tests or checks after making changes when practical.
- Fix regressions caused by the change before considering the task complete.

## Git Safety

- Never run `git push`, `git push --force`, or any command that publishes local commits to a remote unless the user explicitly changes this rule for the current task.
- Local Git inspection commands such as `git status` and `git log` are allowed.
- `git add` and `git commit` are allowed when they are part of the requested workflow.
- Do not amend, rewrite, reset, rebase, or otherwise rewrite Git history unless explicitly requested.

## Secrets and Environment Files

- Do not read, print, expose, edit, create, or overwrite `.env` files or files whose purpose is to store secrets unless the user explicitly requests access to that specific file and confirms it is safe to do so.
- Do not output secret values into logs, terminal output, source files, documentation, commit messages, or chat responses.
- Prefer example/template files such as `.env.example` when configuration documentation is needed.

## Destructive Filesystem Operations

- Ask before deleting or moving files/directories when the operation is destructive, broad, or difficult to reverse.
- Never use recursive deletion merely as a cleanup shortcut when a narrower operation is available.

## Coding Standards

The detailed coding and documentation rules live in `docs/coding-standards.md`; do not duplicate or weaken them here.
