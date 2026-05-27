<#
.SYNOPSIS
  Publish a SANITIZED snapshot of `main` to the PUBLIC remote (violetanvil/kcdx).

.DESCRIPTION
  This repo IS the private repo. `main` tracks the `private` remote and a bare
  `git push` goes there. `.gitignore` is simply the private repo's ignore list —
  ordinary git semantics. This script is the ONLY path that touches the `public`
  remote.

  The public projection is an ALLOWLIST, not a denylist: it publishes ONLY the
  explicitly-listed public directories + root files below. Anything not on the
  list — any new dir, any private working material — defaults to PRIVATE and is
  never published. (A denylist would leak the moment a new private dir was added
  and forgotten; an allowlist fails safe toward private.)

  Because the projection starts from the committed tree, it also inherently
  respects `.gitignore`: ignored files are not tracked, so they are never in the
  tree to publish.

  The script builds the public tree in a throwaway worktree, so it never touches
  the live working tree / index / HEAD — safe to run while other chats share the
  tree. The public branch is a single snapshot commit on an unrelated history;
  each publish force-pushes a fresh snapshot.

.PARAMETER DryRun
  Show what would be published (file count + diffstat vs current public) without
  pushing.

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

# ALLOWLIST — the ONLY directories published. Everything else is private.
# A path is published iff its first segment is one of these. Add a dir here to
# make it public; omit it to keep it (and anything new) private by default.
$PublicDirs = @(
  'src',
  'include',
  'vendor',
  'data',
  'examples',
  'kcdx-engine',
  'test-plugins',
  'tools',
  'docs'           # all of docs/ is public
)

# ALLOWLIST — the ONLY root-level files published. Note: `.gitignore` is
# intentionally NOT published (its contents would reveal the hidden private
# dirs). `CLAUDE.md`, `publish-public.ps1`, etc. are absent by omission.
$PublicRootFiles = @(
  'README.md',
  'LICENSE',
  'CMakeLists.txt',
  'build.ps1',
  'package-release.ps1'
)
# ----------------------------------------------------------------------------

$repoRoot = (git rev-parse --show-toplevel).Trim()
Set-Location $repoRoot

$srcCommit = (git rev-parse $PrivateBranch).Trim()
Write-Host "Source (private $PrivateBranch): $srcCommit"

# Best-effort fetch of the public ref to diff against.
git fetch $PublicRemote $PublicBranch 2>$null | Out-Null

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("kcdx-publish-" + [guid]::NewGuid().ToString('N'))
$cleanupBranch = "publish/tmp-" + [guid]::NewGuid().ToString('N').Substring(0,8)

# Build the set of allowed top-level segments once.
$allowedDirs  = [System.Collections.Generic.HashSet[string]]::new()
$PublicDirs | ForEach-Object { [void]$allowedDirs.Add($_) }
$allowedFiles = [System.Collections.Generic.HashSet[string]]::new()
$PublicRootFiles | ForEach-Object { [void]$allowedFiles.Add($_) }

try {
  git worktree add --quiet -b $cleanupBranch $tmp $srcCommit | Out-Null

  Push-Location $tmp
  try {
    # Every tracked file at the source commit (already respects .gitignore —
    # ignored files are not tracked). Forward slashes from git ls-files.
    # -c core.quotepath=false: emit raw UTF-8 paths, NOT octal-escaped + quoted,
    # so a non-ASCII filename (e.g. an em-dash) is not mis-partitioned by the
    # leading quote turning its top segment into `"docs` instead of `docs`.
    $allFiles = git -c core.quotepath=false ls-files

    # Partition by the allowlist. A file is published iff:
    #   - it is a root file (no '/') AND in $PublicRootFiles, or
    #   - its first path segment is in $PublicDirs.
    $toRemove = [System.Collections.Generic.List[string]]::new()
    $keptCount = 0
    foreach ($f in $allFiles) {
      $slash = $f.IndexOf('/')
      if ($slash -lt 0) {
        $keep = $allowedFiles.Contains($f)
      } else {
        $top = $f.Substring(0, $slash)
        $keep = $allowedDirs.Contains($top)
      }
      if ($keep) { $keptCount++ } else { $toRemove.Add($f) }
    }

    # Remove the non-allowlisted files from the index (and disk in this throwaway
    # tree). Batch through a temp file to avoid a multi-thousand-arg command line.
    if ($toRemove.Count -gt 0) {
      $listFile = Join-Path $tmp '.publish-remove-list'
      # NUL-delimited so paths with spaces/unicode are safe.
      [System.IO.File]::WriteAllText($listFile, ($toRemove -join "`0"))
      # git rm reads -z pathspecs from stdin via --pathspec-from-file.
      git rm -q --cached --ignore-unmatch --pathspec-from-file=$listFile --pathspec-file-nul | Out-Null
      Remove-Item $listFile -Force -ErrorAction SilentlyContinue
    }

    Write-Host ""
    Write-Host "Allowlist (published): $($PublicDirs -join ', ')"
    Write-Host "Root files: $($PublicRootFiles -join ', ')"
    Write-Host "Public projection: $keptCount files (held private: $($toRemove.Count))."

    if ($DryRun) {
      Write-Host ""
      $tree = (git write-tree).Trim()
      $publicRef = "$PublicRemote/$PublicBranch"
      if (git rev-parse --verify --quiet $publicRef) {
        Write-Host "[Dry run] Diffstat vs current ${publicRef}:"
        git diff --stat $publicRef $tree
      } else {
        Write-Host "[Dry run] (no $publicRef yet — first publish)"
      }
      Write-Host ""
      Write-Host "[Dry run] Top-level entries that WOULD be on public:"
      git -c core.quotepath=false ls-files | ForEach-Object { ($_ -split '/')[0] } | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
      Write-Host ""
      Write-Host "[Dry run] No push performed."
      return
    }

    $stamp = Get-Date -Format 'yyyy-MM-dd'
    git commit -q -m "kcdx $stamp"
    $snapCommit = (git rev-parse HEAD).Trim()

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
  git worktree remove --force $tmp 2>$null | Out-Null
  git branch -D $cleanupBranch 2>$null | Out-Null
}
