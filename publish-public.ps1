<#
.SYNOPSIS
  Publish a SANITIZED snapshot of `main` to the PUBLIC remote (violetanvil/kcdx).

.DESCRIPTION
  This repo is the comprehensive PRIVATE tree. `main` tracks the `private` remote
  and `git push` (no args) goes there. This script is the ONLY path that touches
  the `public` remote. It does NOT switch your working branch or disturb the
  working tree — it builds the sanitized tree entirely in git's object store via
  a temporary worktree, so it is safe to run while other chats share this tree.

  Sanitize set (stripped from the public projection) — keep in sync with the
  private paths that .gitignore intentionally no longer ignores:
    .claude/  CLAUDE.md  _research/  third-party-ghidra/
    test-fixtures/  docs/outstanding-work/  docs/known-issues/

  The public branch (origin/main) is a SNAPSHOT, not shared history with private.
  Each publish replaces public main with a single sanitized commit whose tree
  matches the current private main minus the sanitize set. This is a force-push
  to public by design (the histories are unrelated).

.PARAMETER DryRun
  Show what would be published (the stripped file list + diffstat vs current
  public main) and do NOT push.

.EXAMPLE
  pwsh ./publish-public.ps1 -DryRun
  pwsh ./publish-public.ps1
#>
[CmdletBinding()]
param(
  [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

# --- config -----------------------------------------------------------------
$PrivateBranch = 'main'         # source of truth (private remote tracks this)
$PublicRemote  = 'public'       # violetanvil/kcdx
$PublicBranch  = 'main'         # branch on the public remote to receive the snapshot

# Paths stripped from the public projection. Directory entries strip recursively.
$SanitizeSet = @(
  '.claude',
  'CLAUDE.md',
  '_research',
  'third-party-ghidra',
  'test-fixtures',
  'docs/outstanding-work',
  'docs/known-issues'
)
# ----------------------------------------------------------------------------

$repoRoot = (git rev-parse --show-toplevel).Trim()
Set-Location $repoRoot

# Refuse to publish a dirty index/tree for the source branch's tracked paths is
# unnecessary: we publish from the committed tip of $PrivateBranch, never the
# working tree. Uncommitted local edits are simply not included — by design.
$srcCommit = (git rev-parse $PrivateBranch).Trim()
Write-Host "Source (private $PrivateBranch): $srcCommit"

# Make sure we have the latest public ref to diff against (best-effort).
git fetch $PublicRemote $PublicBranch 2>$null | Out-Null

# Build the sanitized tree in an isolated temporary worktree so the live working
# tree / index / HEAD are never touched (safe under parallel chats).
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("kcdx-publish-" + [guid]::NewGuid().ToString('N'))
$cleanupBranch = "publish/tmp-" + [guid]::NewGuid().ToString('N').Substring(0,8)

try {
  # Detached worktree at the private tip — its own HEAD/index, shared object store.
  git worktree add --quiet -b $cleanupBranch $tmp $srcCommit | Out-Null

  Push-Location $tmp
  try {
    # Strip the sanitize set from the index (and disk in this throwaway tree).
    $stripped = @()
    foreach ($p in $SanitizeSet) {
      # --ignore-unmatch: a path absent at this commit is not an error.
      $before = (git ls-files -- $p | Measure-Object).Count
      git rm -r --quiet --cached --ignore-unmatch -- $p | Out-Null
      if ($before -gt 0) { $stripped += "$p ($before files)" }
    }

    # Also drop the publish script itself and the accounting scratch file — they
    # are private tooling, not public artifacts.
    git rm -q --cached --ignore-unmatch -- 'publish-public.ps1' 'REPO-RECONCILIATION-ACCOUNTING.md' | Out-Null

    Write-Host ""
    Write-Host "Stripped from public projection:"
    $stripped | ForEach-Object { Write-Host "  - $_" }

    $publicCount = (git ls-files | Measure-Object).Count
    Write-Host ""
    Write-Host "Public projection will contain $publicCount tracked files."

    if ($DryRun) {
      Write-Host ""
      Write-Host "[Dry run] Diffstat vs current $PublicRemote/$PublicBranch (if present):"
      $tree = (git write-tree).Trim()
      $publicRef = "$PublicRemote/$PublicBranch"
      if (git rev-parse --verify --quiet $publicRef) {
        git diff --stat $publicRef $tree
      } else {
        Write-Host "  (no $publicRef yet — first publish)"
      }
      Write-Host ""
      Write-Host "[Dry run] No push performed."
      return
    }

    # Commit the sanitized tree as a single snapshot commit.
    $stamp = Get-Date -Format 'yyyy-MM-dd'
    git commit -q -m "Public snapshot $stamp (sanitized from private $($srcCommit.Substring(0,8)))."
    $snapCommit = (git rev-parse HEAD).Trim()

    # Force-push the snapshot to public main. Force is required: public history is
    # intentionally unrelated to private, and each publish is a fresh snapshot.
    Write-Host ""
    Write-Host "Pushing snapshot $($snapCommit.Substring(0,8)) -> $PublicRemote/$PublicBranch ..."
    git push --force $PublicRemote "HEAD:$PublicBranch"
    Write-Host "Published."
  }
  finally {
    Pop-Location
  }
}
finally {
  # Always remove the temporary worktree + its throwaway branch.
  git worktree remove --force $tmp 2>$null | Out-Null
  git branch -D $cleanupBranch 2>$null | Out-Null
}
