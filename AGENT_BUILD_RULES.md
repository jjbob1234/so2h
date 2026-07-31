# Build/CI Rules for AI Agents Working in This Repo

These are binding rules ("law") for any AI agent (Claude, GPT, or otherwise) doing build/CI
work in this repo (so2h / 2s2h, and any of its sibling env/ projects run the same way). Read
this before touching CI on any branch here.

## Definition: "Completion status"
A build is only "complete" when **all executable builds currently applicable** (e.g.
windows/macos/linux, whichever are relevant to the change) have **succeeded**, AND a build
artifact is **currently available** to hand to the user. Partial success (e.g. windows green,
macos still building or skipped when it shouldn't be) is not complete.

## 1. Scheduling checks after a build is triggered
1. When a build/CI run is initiated (push triggers it), automatically schedule the first status
   check for **15 minutes** after initiation. Do not ask the user to say "check" first.
2. At the 15-minute check:
   - **Not done yet** -> switch to checking every **5 minutes** afterward until it resolves.
     Each of these 5-minute checks must be **documented** (what was checked, status, timestamp)
     so there's a record of how long the build actually took.
   - **Done** -> document the final build time (start -> completion duration) and proceed
     straight to step 4 (user presentation). No need to keep polling.

## 2. Handling repeated build failures
- Max **5 build attempts total** before stopping to ask for human intervention.
- Use judgment on when to stop earlier than 5, based on how similar the failures are:
  - If failures are clearly related/clustered (e.g. 2-3 failures all tracing to the same area,
    like icon rendering) and a fix isn't converging, stop and escalate rather than burning all
    5 attempts on the same category of problem.
  - If failures are unrelated/scattered ("random") and 5 have happened in a row, stop
    regardless.
- **On stop**: write a report covering:
  - Every problem hit so far and the fix applied (or attempted) for each.
  - Which file(s) each problem/fix came from.
  - An explicit statement that this needs **human intervention** for a broader/structural fix,
    not more ad-hoc patching — the agent should not keep guessing indefinitely.
- Rely on human intervention occasionally when it's the better call, even before hitting the
  attempt cap, if the pattern suggests a deeper design issue rather than a quick fix.

## 3. Presenting a finished build to the user
Once a build is complete (per the definition above) and a build artifact/zip is available:
1. **Actually attach/deliver the build zip** to the user — every time. Never describe a
   finished build without also handing over the artifact.
2. Alongside the zip, write a short summary covering:
   - Every change made to the code (file-by-file or logically grouped).
   - The **source of the logic/facts** for anything non-obvious or ported from elsewhere —
     e.g. "had to reuse OOT/SOH's menu navigation and tie it into the MM/2S2H interface via a
     patch, because this OOT/SOH area wouldn't load without a script call from MM/2S2H, so a
     patch was written to bridge that" — so the user can trace *why* a given approach was taken
     and where the pattern/reference came from (OOT decomp, SOH source, MM decomp, 2s2h
     conventions, etc).
