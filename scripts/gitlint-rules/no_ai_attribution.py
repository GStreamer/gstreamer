# Gitlint user-defined rule: reject AI tool attribution in commit messages.
#
# Loaded through `extra-path` in .gitlint, so it runs both in the local
# commit-msg hook (installed by `gitlint install-hook` during `meson setup`)
# and in CI (`gitlint --commits ...` in the pre-commit checks job).
#
# Pattern sources: the CHAOSS disclosure detector constants
# (github.com/chaoss/disclosure) and the ComfyUI AI-trailer blocklist,
# cross-checked against per-tool docs. Two matching principles:
#
# - Email-shaped patterns match tool-owned addresses anywhere on a line.
# - Name-shaped patterns are anchored to a trailer prefix, a line-initial
#   attribution statement, or a tool session URL. A bare tool name that is
#   also a human name (Claude, Cody, Devin, Jules, Cline) never matches on
#   its own.

import re

from gitlint.rules import CommitRule, RuleViolation

# Identifiers owned by AI tools/bots; matchable anywhere on a line.
_TOOLS_BY_IDENTITY = (
    "noreply@anthropic\\.com"
    r"|codex@openai\.com|noreply@openai\.com|chatgpt-codex-connector|openai-codex"
    r"|copilot@github\.com|copilot(?:-swe-agent)?[^\s<]*@users\.noreply\.github\.com"
    r"|\[bot\]@"  # any GitHub App bot
    r"|cursoragent@cursor\.com"
    r"|(?:noreply@)?aider\.chat"
    r"|noreply@windsurf\.com|@codeium\.com|qodo\.com"
    r"|noreply@continue\.dev|continue-agent"
    r"|(?:gemini|jules|gemini-code-assist)@google\.com"
    r"|devin(?:@cognition\.ai|-ai-integration)"
    r"|cline(?:@cline\.ai|-bot)"
    r"|openhands@all-hands\.dev"
    r"|noreply@(?:codebuff|freebuff)\.com"
    r"|qwen-coder@alibabacloud\.com"
    r"|noreply@z\.ai"
)

# Tool names that are unambiguous in an attribution trailer (no human names).
_TOOLS_IN_TRAILER = (
    r"claude[\s-]?code|copilot|codex|chatgpt|openai|gemini|cursor"
    r"|windsurf|codeium|qodo|continue\.?dev|sourcegraph"
    r"|amazon[\s-]?q|junie|jetbrains|roo[\s-]?code|openhands"
    r"|replit|tabnine|qwen|augment"
)

_PATTERNS = [
    (re.compile(_TOOLS_BY_IDENTITY, re.IGNORECASE), "an AI tool"),
    (re.compile(rf"^co-authored-by\s*:\s*[^>\n]*\b(?:{_TOOLS_IN_TRAILER})\b", re.IGNORECASE | re.MULTILINE), "an AI tool"),
    # Line-initial attribution footer naming a tool ("Generated with X",
    # "🤖 Generated using X", "With help from X"); requires a tool name on the
    # same line so non-tool phrasing passes.
    (re.compile(
        r"^\s*(?:🤖\s*)?(?:(?:generated|drafted|written|created|authored|co-authored|produced)"
        r"\s+(?:with|by|using|via)\b|(?:with|via)\s+(?:the\s+)?(?:help|assistance)\s+(?:of|from))"
        r".*\b(?:claude|copilot|codex|chatgpt|openai|gemini|cursor|aider|windsurf|codeium"
        r"|qodo|continue|sourcegraph|amazon[\s-]?q|junie|jetbrains|roo|openhands|replit"
        r"|tabnine|qwen|augment|antigravity)\b",
        re.IGNORECASE), "an AI tool"),
    # Tool session trailers/URLs.
    (re.compile(r"^\s*(?:claude-session|replit-commit-author|entire-(?:metadata|session|agent))\s*:", re.IGNORECASE), "a tool session"),
    (re.compile(r"claude\.ai/code/session_", re.IGNORECASE), "a Claude session link"),
    (re.compile(r"^aider:", re.IGNORECASE), "aider"),  # aider's auto-added title prefix
]


class NoAIAttribution(CommitRule):
    """Commit messages must not attribute co-authorship to AI tools."""

    id = "GST001"
    name = "no-ai-attribution"

    def validate(self, commit):
        violations = []
        for lineno, text in enumerate(str(commit.message).splitlines(), start=1):
            for pattern, tool in _PATTERNS:
                if pattern.search(text):
                    msg = (
                        f"Commit message attributes a contribution to {tool}; "
                        "remove the AI attribution line"
                    )
                    violations.append(
                        RuleViolation(self.id, msg, content=text, line_nr=lineno)
                    )
                    break
        return violations
