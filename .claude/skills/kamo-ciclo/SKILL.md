---
name: kamo-ciclo
description: Run one full KAMO development cycle - kamo-programmer implements, kamo-tester tests, kamo-pr opens the pull request - for a task the maintainer describes. Use when the maintainer types /kamo-ciclo <task>.
---

# KAMO development cycle

The maintainer gives a task (in Spanish). Run it through the three project agents in order.
Talk to the maintainer in Spanish; agents work in English.

1. **Program.** Start the `kamo-programmer` agent with the task, translated to English, plus
   any evidence you already have (log lines, addresses, steps). Wait for its report: branch,
   changes, build result.
   - If it stopped because a fix needs code from another project, ask the maintainer before
     going on.
   - If the build failed, send it back to kamo-programmer with the error, at most twice.
2. **Test.** Start the `kamo-tester` agent: run `tools/kamo_test.ps1` on that build, plus any
   test specific to the task. Wait for PASS/FAIL per test.
   - On a FAIL, send the failure evidence back to kamo-programmer (same branch), then test
     again. Stop after three rounds and report to the maintainer.
3. **Pull request.** Only when every test passed, start the `kamo-pr` agent for the branch and
   get the PR URL.
4. **Report** to the maintainer in Spanish: what changed, the test table, the PR link, and what
   still needs a person (for example a real match between two PCs). Never merge or publish a
   release yourself; ask the maintainer.
