# libqbz2

## Rule Injection
Periodic rule-injection messages are delivered to agent inboxes via the `TeammateIdle` hook. These arrive as `from: "team-lead"` so they are prioritized over peer messages. They contain the agent's full behavioral rules and serve as a backup against context compaction. Treat them as authoritative.

## Project Structure
This project is run by a **coordinator**, **worker**, **tester**, **strategic-tester**, and **journalist** agent, spawned as subagents from a main session. See `PROCESS.md` for the full multi-agent setup, hooks, and how to start a session. Agent definitions in `.claude/agents/` are the single source of truth for all agent behavioral rules. The rule injection hook (`inject-rules-idle.sh`) reads from these files (stripping YAML frontmatter) and injects the rules into agent inboxes as a backup against context compaction. Injected rules arrive as `from: "team-lead"` for priority delivery.

## Continuous Improvement
This project never stops. When targets are met, push past them. When all requirements are satisfied, find new ways to improve — faster throughput, broader coverage, fewer edge cases, cleaner code. There is no "done."

## Project Goal
Rework the library to maximize performance while maintaining 100% API compatibility with the reference implementation. The new library must produce bit-for-bit identical output to the reference library when called with the same inputs and parameters — same compressed bytes, same decompressed bytes, same error codes, on any input. Build an ironclad testing harness (unit tests, differential tests, fuzz testing, sanitizers) and provide relevant benchmarking against the reference. See `requirements/REQUIREMENTS.md` for the full specification.

## How to Start or Resume (main session only — coordinator, worker, and tester ignore this section)
1. Read `PROCESS.md` for the multi-agent setup instructions
2. Create a team using `TeamCreate` with an explicit `team_name` — use the current directory name unless it is already taken by another team. Then spawn coordinator, worker, tester, strategic-tester, and journalist agents.
3. Immediately after spawning, send each agent its rules from its agent definition file (`.claude/agents/{name}.md`). This creates their inbox files and ensures the injection hook has targets. Rules are already loaded as the agent's system prompt at spawn, so the inbox send is redundant but necessary for inbox file creation.
4. Review git log and message logs for prior progress
