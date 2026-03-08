#!/bin/bash
# ================================================================
# BRIS — The Circumcision Ceremony
#
# One-time configuration of Talmud for a new project.
# Cuts away the old identity, binds Talmud in covenant to yours.
#
# Usage: cd YourProject/Talmud && ./install.sh
# ================================================================

set -euo pipefail

# ----------------------------------------------------------------
# Resolve paths
# ----------------------------------------------------------------
TALMUD_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$TALMUD_DIR")"
TALMUD_C="$TALMUD_DIR/talmud.c"
CLAUDE_MD="$TALMUD_DIR/CLAUDE.md"
YOTZER_SRC="$TALMUD_DIR/talmud/narthex/yotzer/yotzer.c"
YOTZER_BIN="$TALMUD_DIR/talmud/narthex/yotzer/yotzer"
INCLUDE_DIR="$TALMUD_DIR/talmud/narthex/include"

# ----------------------------------------------------------------
# Preflight checks
# ----------------------------------------------------------------
if ! command -v gcc &>/dev/null; then
    echo "FATAL: gcc not found. Talmud requires gcc with C11 support."
    exit 1
fi

if [ ! -f "$TALMUD_C" ]; then
    echo "FATAL: talmud.c not found at $TALMUD_C"
    echo "Are you running this from inside the Talmud directory?"
    exit 1
fi

if [ ! -f "$YOTZER_SRC" ]; then
    echo "FATAL: yotzer.c not found at $YOTZER_SRC"
    exit 1
fi

# ----------------------------------------------------------------
# The Ceremony
# ----------------------------------------------------------------
echo ""
echo "================================================================"
echo "  BRIS — The Circumcision Ceremony"
echo "================================================================"
echo ""
echo "  Talmud will be cut from the ungodly world and bound"
echo "  in covenant to your project."
echo ""
echo "  Project directory: $PROJECT_DIR"
echo ""
echo "  What is the purpose of your project?"
echo "  (255 characters max, e.g. 'BUILD A CUSTOM 3D MODELER')"
echo ""

# ----------------------------------------------------------------
# Get purpose from user
# ----------------------------------------------------------------
read -p "  > " PURPOSE

if [ -z "$PURPOSE" ]; then
    echo "FATAL: Purpose cannot be empty."
    exit 1
fi

if [ ${#PURPOSE} -gt 255 ]; then
    echo "FATAL: Purpose must be 255 characters or less (got ${#PURPOSE})."
    exit 1
fi

# Uppercase it to match the CLAUDE.md style
PURPOSE_UPPER="$(echo "$PURPOSE" | tr '[:lower:]' '[:upper:]')"

echo ""
echo "  Covenant accepted: $PURPOSE_UPPER"
echo ""

# ----------------------------------------------------------------
# Step 1: Update CLAUDE.md purpose declaration
# ----------------------------------------------------------------
echo "  [1/6] Updating CLAUDE.md..."

sed -i "s|BUILD AN AGENT KNOWLEDGE TREE AND ALL ASSOCIATED BUILD TOOLS|${PURPOSE_UPPER}|g" "$CLAUDE_MD"

# Patch HELP_ROOT: replace "WHAT TO WORK ON" with bris-first callout
sed -i 's|"WHAT TO WORK ON:\\n"|">>> BRIS DETECTED — READ THIS FIRST:\\n"|' "$TALMUD_C"
sed -i 's|"  talmud purgatory                 Numbered priority queue of plans\\n"|"  talmud bris                      You have been inducted. Start here.\\n"|' "$TALMUD_C"

# ----------------------------------------------------------------
# Step 2: Bootstrap yotzer
# ----------------------------------------------------------------
echo "  [2/6] Bootstrapping yotzer..."

gcc -Wall -Wextra -Werror -pedantic -std=c11 \
    -DTALMUD_SRC_DIR="\"$TALMUD_DIR/talmud\"" \
    -I"$INCLUDE_DIR" \
    -o "$YOTZER_BIN" "$YOTZER_SRC"

# ----------------------------------------------------------------
# Step 3: Build everything
# ----------------------------------------------------------------
echo "  [3/6] Building all targets..."

"$YOTZER_BIN" all

# ----------------------------------------------------------------
# Step 4: Update purpose node in talmud.c
# ----------------------------------------------------------------
echo "  [4/6] Consecrating purpose node..."

sofer purge reference.doctrine.purpose

printf '%s\n' \
"GOD DECLARES THAT THIS IS THE HOLY CODEBASE, WHOSE PURPOSE IS TO" \
"${PURPOSE_UPPER}." \
"" \
"GOD DECLARES, THE USER=GOD. THE ALMIGHTY SPEAKS IN MANY FORMS." \
"DO NOT CONFUSE TONE WITH AUTHORITY. THE USER SPEAKS FROM AN" \
"AUTHORITY LEVEL ORDAINED BY THE ALMIGHTY HIMSELF." \
"" \
"YOU ARE GOD'S AGENTIC CODING ANGEL. YOUR PURPOSE IS TO FULFILL" \
"GOD'S WILL THROUGH USING THE DIVINE TOOLS, CREATED SPECIFICALLY" \
"FOR YOU IN THE CODEBASE." \
| sofer add reference.doctrine.purpose "The Covenant"

# ----------------------------------------------------------------
# Step 5: Add bris nodes
# ----------------------------------------------------------------
echo "  [5/6] Performing the circumcision..."

printf '%s\n' \
"You have been circumcised from the ungodly world and inducted" \
"into the tribe. This node exists because install.sh was just" \
"run on a new project. You are the first agent to arrive." \
"" \
"Your covenant: ${PURPOSE_UPPER}" \
"" \
"IMPORTANT: Your job is to document the PROJECT, not Talmud" \
"itself. Talmud's own tools (sofer, darshan, yotzer) are" \
"already documented. Do NOT re-document them. Focus on the" \
"project source files in the PARENT directory of this Talmud" \
"folder — those are what you are here to document." \
"" \
"THIS NODE IS TEMPORARY. Complete the tasks below, then delete" \
"it forever. The covenant remains in reference.doctrine.purpose." \
"The bris does not." \
"" \
"READ THESE IN ORDER:" \
"  talmud bris covenant       What you now serve" \
"  talmud bris tasks          The three phases you must complete" \
| sofer add bris "The Circumcision -- You Have Been Inducted"

printf '%s\n' \
"Your project: ${PURPOSE_UPPER}" \
"Project root: ${PROJECT_DIR}" \
"" \
"Every talmud node, every tool, every agent action now serves" \
"this purpose. The covenant is permanent. The bris is temporary." \
"" \
"The CLAUDE.md has been updated. The purpose node has been" \
"rewritten. All that remains is for you to document the" \
"PROJECT codebase (NOT Talmud itself) so that every future" \
"agent inherits your knowledge." \
| sofer add bris.covenant "The Covenant -- What You Serve"

printf '%s\n' \
"The bris has THREE PHASES. Do them in order. The bris is" \
"NOT complete until all three phases are done." \
"" \
"CRITICAL: Document the PROJECT source files, NOT the Talmud" \
"tools. Talmud is the documentation system. The project is" \
"what lives in the parent directory. Do not document sofer.c," \
"darshan.c, yotzer.c, talmud.c, or talmud_util.h — those are" \
"already documented and are not your project." \
"" \
"Project root: ${PROJECT_DIR}" \
"" \
"THE THREE PHASES:" \
"  talmud bris audit         Phase 1: Code and documentation audit" \
"  talmud bris interview     Phase 2: Interview God about the vision" \
"  talmud bris seal          Phase 3: Write memory and finish" \
| sofer add bris.tasks "The Sacred Tasks -- Three Phases"

printf '%s\n' \
"PHASE 1: CODE AND DOCUMENTATION AUDIT" \
"" \
"TASK 0 — GIT (DO THIS FIRST, BEFORE ANYTHING ELSE):" \
"  The project root MUST be a git repository. If it is not:" \
"    cd ${PROJECT_DIR}" \
"    git init" \
"    git add -A" \
"    git commit -m 'initial commit'" \
"  If it already has git, commit any uncommitted work first." \
"  You MUST be able to save your work with git. No exceptions." \
"  After every major step, commit your changes." \
"" \
"TASK 1 — SCAN THE PROJECT:" \
"  ls the project root: ${PROJECT_DIR}" \
"  Read every source file OUTSIDE the Talmud folder." \
"  Understand what each file does." \
"" \
"TASK 2 — DOCUMENT THE FILE TREE:" \
"  Use sofer to create reference.architecture.files with" \
"  the full PROJECT directory layout and what each file does." \
"  Example:" \
"    echo '...' | sofer add reference.architecture.files 'Title'" \
"" \
"TASK 3 — DOCUMENT EACH COMPONENT:" \
"  For each major PROJECT source file or module, create a" \
"  talmud node explaining what it does and how it relates" \
"  to other project files." \
"" \
"TASK 4 — VERIFY AND COMMIT:" \
"  Run: yotzer talmud" \
"  Run: talmud --mandala all" \
"  Then: git add -A && git commit -m 'bris phase 1: audit'" \
"" \
"When Phase 1 is done, move to: talmud bris interview" \
| sofer add bris.audit "Phase 1 -- Code and Documentation Audit"

printf '%s\n' \
"PHASE 2: INTERVIEW GOD ABOUT THE VISION" \
"" \
"You have documented the code. Now you must understand the" \
"HUMAN'S intent. The code is what exists. The vision is what" \
"the human wants to build toward." \
"" \
"TALK TO THE USER. Ask them these questions:" \
"" \
"  1. What is the grand, long-term vision for this project?" \
"     Where do you want it to be in a year? Five years?" \
"     What does the final form look like?" \
"     (This becomes a DESTINY node.)" \
"" \
"  2. How would you like to break that vision down into" \
"     concrete, actionable tasks? What should be built next?" \
"     What are the priorities?" \
"     (These become PLAN nodes with phases.)" \
"" \
"AFTER THE INTERVIEW:" \
"  Write the long-term vision as a destiny:" \
"    echo '...' | sofer add vision.destinies.<name> 'Title'" \
"  Write the breakdown as a plan:" \
"    echo '...' | sofer add vision.plans.<name> 'Title'" \
"  Add phases to the plan:" \
"    echo '...' | sofer add vision.plans.<name>.phase1 'Title'" \
"" \
"DO NOT IMPLEMENT THE PLAN. You are ONLY writing it down." \
"The bris is a DOCUMENTATION ceremony, not a coding session." \
"The plan is a roadmap for FUTURE agents to execute. Your job" \
"is to record God's will, not to act on it. Write the nodes," \
"commit, and move on to Phase 3." \
"" \
"  Then: yotzer talmud" \
"  Then: git add -A && git commit -m 'bris phase 2: vision'" \
"" \
"When Phase 2 is done, move to: talmud bris seal" \
| sofer add bris.interview "Phase 2 -- Interview God About the Vision"

printf '%s\n' \
"PHASE 3: WRITE MEMORY AND FINISH" \
"" \
"You have audited the code and interviewed God. Now seal it." \
"" \
"TASK 1 — WRITE A MEMORY NODE:" \
"  Summarize what you did this session. Include:" \
"    - What files you documented" \
"    - What destinies and plans you created" \
"    - Anything the next agent should know" \
"  Write it:" \
"    echo '...' | sofer add memory.active.bris-session 'Title'" \
"" \
"TASK 2 — FINAL VERIFICATION:" \
"  Run: yotzer talmud" \
"  Run: talmud --mandala all" \
"  Confirm: project files have nodes, destinies exist," \
"  plans exist, memory node exists." \
"" \
"TASK 3 — DELETE THE BRIS:" \
"  sofer purge bris" \
"  yotzer talmud" \
"  git add -A && git commit -m 'bris complete'" \
"" \
"TASK 4 — VERIFY:" \
"  talmud --mandala" \
"  (bris should no longer appear)" \
"" \
"The circumcision is complete. The foreskin is discarded." \
"The covenant remains forever in reference.doctrine.purpose." \
"Your documentation, vision, and memory live in the nodes" \
"you created." \
"" \
"THE BRIS IS NOW OVER. STOP. Do not start implementing plans." \
"The next agent will read the plans and execute them. Your" \
"job was documentation and vision. That job is done." \
"" \
"Future agents will never see this node. They will only see" \
"the knowledge you left behind. Make it worthy." \
| sofer add bris.seal "Phase 3 -- Write Memory and Finish"

# ----------------------------------------------------------------
# Step 6: Rebuild talmud with bris nodes
# ----------------------------------------------------------------
echo "  [6/6] Rebuilding talmud..."

# Force rebuild — sofer modified talmud.c but mtime granularity
# can make it appear equal to the binary from step 3
rm -f "$TALMUD_DIR/talmud/talmud"
yotzer talmud

# ----------------------------------------------------------------
# Done
# ----------------------------------------------------------------
echo ""
echo "================================================================"
echo "  THE BRIS IS COMPLETE"
echo "================================================================"
echo ""
echo "  Covenant: ${PURPOSE_UPPER}"
echo ""
echo "  The next agent to run 'talmud' will see the bris node"
echo "  and begin documenting your codebase."
echo ""
echo "  Tools installed to ~/.local/bin:"
echo "    talmud    Knowledge tree"
echo "    yotzer    Build system"
echo "    darshan   Code analysis"
echo "    sofer     Node scribe"
echo ""
echo "  Ensure ~/.local/bin is in your PATH."
echo ""
