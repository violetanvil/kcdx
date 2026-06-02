## This repo's governance additions

- A governance change to `.claude/rules/anti-patterns.md` or `.claude/anti-pattern-rationale.md` additionally requires the corresponding consent guard's accept-prompt to have fired before the commit: the system consent guard gates `anti-patterns.md`; the repo `guard-rationale-consent.py` gates `anti-pattern-rationale.md`. An AP whose rationale was not blessed is unauthorized.
