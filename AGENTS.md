# Emojicode agent workflow

The [Emojicode project](https://github.com/users/DylanJones/projects/1) tracks work by issue. Use its `Status` field as the handoff between implementation, review, and Dylan's merge decision. The GitHub CLI is the supported way to update the board; it needs the `project` OAuth scope (`gh auth refresh -s project` if missing).

## Moving work

1. When starting an issue, move it to **In progress**. Open a PR that links the issue (for example, `Fixes #123` when merging the PR should close it).
2. Once the PR is ready for review, move the issue to **In review**. Include the test results and any dependencies in the PR description.
3. A reviewer records findings on the PR and chooses exactly one outcome:
   - **Ready to merge:** No blocking findings remain, required checks pass, and dependencies are resolved. Leave a review summary on the PR and move the issue to **Ready to merge**. Dylan performs the merge.
   - **Changes requested:** Leave actionable comments on the PR and move the issue to **Changes requested**. The implementing agent moves it to **In progress** when taking the feedback, addresses the comments, reports new tests, then moves it back to **In review**.
   - **Needs decision:** Leave a PR comment with the question, options, and consequences, then move the issue to **Needs decision**. Wait for Dylan's decision before changing the implementation or marking it ready to merge. After the decision, move it to **In progress** or **In review**, as appropriate.
4. A new code commit after a ready-to-merge verdict returns the issue to **In review** for the changed diff. After Dylan merges and the linked issue closes, the project's enabled **Item closed** workflow moves it to **Done**.

Move an issue with:

```sh
gh project item-edit 1 --owner DylanJones \
  --url https://github.com/DylanJones/emojicode/issues/123 \
  --field Status --value 'In review'
```

Use the same command with the outcome status for each linked issue when a PR addresses several issues. If a PR has no issue, add the PR itself with `gh project item-add 1 --owner DylanJones --url <PR URL>` and update that PR item instead.
Link the PR before setting the review status: the project's enabled PR-link workflow can change a card's status. Check the card after linking or closing a superseded PR, especially when several PRs link to the same issue.

All current PRs are authored as `DylanJones`, the same identity used by `gh` here. GitHub does not permit formal approval of one's own PR, so an agent reviewer should state **"Review verdict: ready to merge"** in a PR comment and set the project status. A reviewer using a different GitHub identity can also submit a formal GitHub approval. A project status or agent verdict is a review handoff, not a merge action.

## Commits

Every commit message starts with an emoji as its first character, e.g. `🦁 Fix sorting edge cases`. Choose one
that fits the change.
