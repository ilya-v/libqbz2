#!/bin/bash
# TeammateIdle hook: inject rules into the idle agent's inbox
# Input: JSON with teammate_name, team_name, session_id, cwd, etc.
# Output: nothing on stdout (TeammateIdle doesn't support additionalContext)
# Instead, writes rules directly to the agent's inbox file using mkdir-based locking
# (compatible with proper-lockfile used by Claude Code internally)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LOGS_DIR="$PROJECT_DIR/logs"
LOG="$LOGS_DIR/idle-inject.log"
mkdir -p "$LOGS_DIR" 2>/dev/null
exec 2>>"$LOG"

INPUT=$(cat)
TS=$(date '+%Y-%m-%d %H:%M:%S')
TS_ISO=$(date -u '+%Y-%m-%dT%H:%M:%S.000Z')

# Extract fields from JSON input
TEAMMATE=$(echo "$INPUT" | jq -r '.teammate_name // ""')
TEAM=$(echo "$INPUT" | jq -r '.team_name // ""')
SID=$(echo "$INPUT" | jq -r '.session_id // ""')
CWD=$(echo "$INPUT" | jq -r '.cwd // ""')
EVENT=$(echo "$INPUT" | jq -r '.hook_event_name // ""')

echo "$TS ── TeammateIdle fired ──" >> "$LOG"
echo "$TS   teammate=$TEAMMATE team=$TEAM sid=$SID cwd=$CWD event=$EVENT" >> "$LOG"
echo "$TS   raw input: $INPUT" >> "$LOG"

# Validate: we need teammate_name and team_name
if [ -z "$TEAMMATE" ] || [ -z "$TEAM" ]; then
  echo "$TS   SKIP: missing teammate_name or team_name" >> "$LOG"
  exit 0
fi

# Map teammate name to agent definition file (single source of truth)
AGENTS_DIR="$PROJECT_DIR/.claude/agents"
AGENT_FILE=""
case "$TEAMMATE" in
  coordinator)  AGENT_FILE="$AGENTS_DIR/coordinator.md" ;;
  worker|worker-*)  AGENT_FILE="$AGENTS_DIR/worker.md" ;;
  strategic-tester|strategic-tester-*)  AGENT_FILE="$AGENTS_DIR/strategic-tester.md" ;;
  journalist|journalist-*)  AGENT_FILE="$AGENTS_DIR/journalist.md" ;;
  tester|tester-*)  AGENT_FILE="$AGENTS_DIR/tester.md" ;;
esac

if [ -z "$AGENT_FILE" ]; then
  echo "$TS   SKIP: no agent definition mapping for teammate=$TEAMMATE" >> "$LOG"
  exit 0
fi

if [ ! -f "$AGENT_FILE" ]; then
  echo "$TS   ERROR: agent definition not found: $AGENT_FILE" >> "$LOG"
  exit 0
fi

# Use the agent definition file for rate limiting and content
RULES_FILE="$AGENT_FILE"

# Inbox path
INBOX="$HOME/.claude/teams/$TEAM/inboxes/${TEAMMATE}.json"
LOCK="${INBOX}.lock"

echo "$TS   rules_file=$RULES_FILE" >> "$LOG"
echo "$TS   inbox=$INBOX" >> "$LOG"
echo "$TS   lock=$LOCK" >> "$LOG"

if [ ! -f "$INBOX" ]; then
  echo "$TS   ERROR: inbox file does not exist: $INBOX" >> "$LOG"
  exit 0
fi

# Rate limit: max one injection per agent every 10 minutes
# Override: if the rules file was modified after the last injection, bypass the rate limit
RATE_FILE="$LOGS_DIR/idle-inject-${TEAMMATE}.ts"
INJECT_INTERVAL=600  # 10 minutes in seconds
NOW_EPOCH=$(date +%s)
LAST_INJECT=$(cat "$RATE_FILE" 2>/dev/null || echo 0)
RULES_MTIME=$(stat -c %Y "$RULES_FILE" 2>/dev/null || echo 0)
ELAPSED=$(( NOW_EPOCH - LAST_INJECT ))
if [ "$RULES_MTIME" -gt "$LAST_INJECT" ]; then
  echo "$TS   RATE-OVERRIDE: rules file modified (mtime=$RULES_MTIME > last_inject=$LAST_INJECT), bypassing rate limit" >> "$LOG"
elif [ "$ELAPSED" -lt "$INJECT_INTERVAL" ]; then
  echo "$TS   RATE-LIMITED: last inject ${ELAPSED}s ago (< ${INJECT_INTERVAL}s)" >> "$LOG"
  exit 0
fi
echo "$TS   rate check: last inject ${ELAPSED}s ago, proceeding" >> "$LOG"

# Read rules content, stripping YAML frontmatter (---\n...\n---) if present
RULES=$(awk 'BEGIN{f=0; hf=0} NR==1 && /^---$/{hf=1; f++; next} hf && f<2 && /^---$/{f++; next} f>=2 || hf==0{print}' "$RULES_FILE")
echo "$TS   rules loaded: $(echo "$RULES" | wc -c) bytes (from $RULES_FILE)" >> "$LOG"

# Build the message JSON
MSG=$(jq -n \
  --arg from "team-lead" \
  --arg text "$RULES" \
  --arg ts "$TS_ISO" \
  '{from: $from, text: $text, timestamp: $ts, color: "yellow", read: false}')

echo "$TS   message built: $(echo "$MSG" | wc -c) bytes" >> "$LOG"

# Acquire mkdir-based lock (proper-lockfile compatible)
# proper-lockfile uses mkdir for atomicity, then utimes to set mtime for stale detection.
# Default stale threshold is 10s. We touch the dir after mkdir to match the protocol.
LOCK_ACQUIRED=0
LOCK_WAIT=0
MAX_WAIT=50  # 50 * 0.1s = 5 seconds max
while [ "$LOCK_ACQUIRED" -eq 0 ] && [ "$LOCK_WAIT" -lt "$MAX_WAIT" ]; do
  if mkdir "$LOCK" 2>/dev/null; then
    touch "$LOCK"
    LOCK_ACQUIRED=1
  else
    LOCK_WAIT=$((LOCK_WAIT + 1))
    sleep 0.1
  fi
done

if [ "$LOCK_ACQUIRED" -eq 0 ]; then
  echo "$TS   ERROR: failed to acquire lock after ${MAX_WAIT} attempts: $LOCK" >> "$LOG"
  exit 0
fi

echo "$TS   lock acquired (waited ${LOCK_WAIT} iterations)" >> "$LOG"

# Read-modify-write inbox
RESULT=0
CURRENT=$(cat "$INBOX" 2>/dev/null || echo "[]")
UPDATED=$(echo "$CURRENT" | jq --argjson msg "$MSG" '. += [$msg]' 2>/dev/null)
if [ $? -eq 0 ] && [ -n "$UPDATED" ]; then
  echo "$UPDATED" > "$INBOX"
  RESULT=$?
  MSG_COUNT=$(echo "$UPDATED" | jq 'length' 2>/dev/null || echo "?")
  echo "$TS   wrote inbox: result=$RESULT, total_messages=$MSG_COUNT" >> "$LOG"
else
  echo "$TS   ERROR: jq failed to update inbox JSON" >> "$LOG"
  RESULT=1
fi

# Release lock
rm -rf "$LOCK"
echo "$TS   lock released" >> "$LOG"

if [ "$RESULT" -eq 0 ]; then
  echo "$NOW_EPOCH" > "$RATE_FILE"
  echo "$TS   SUCCESS: injected rules for $TEAMMATE via inbox" >> "$LOG"
else
  echo "$TS   FAILED: could not write to inbox" >> "$LOG"
fi

echo "$TS ── done ──" >> "$LOG"
exit 0
