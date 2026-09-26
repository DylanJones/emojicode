// Checks which documentation section the learner docs hover links each token to. Run with `npm test` after
// `npm run compile`.
const fs = require('fs');
const path = require('path');
const assert = require('assert');
const { LearnerDocument, sections } = require('../out/learnerDocs');

const FISH = `📘 Fish and how they swim. 📘
📗 A fish that can swim. 📗
🐇 🐟 🍇
  🖍🆕 depth 🔢 ⬅️ 0

  📗 Makes the fish swim down by meters. 📗
  ❗️ 🏊 meters 🔢 ➡️ 🔢 🍇
    depth ⬅️➕ meters
    ↩️ depth
  🍉

  🐇❗️ 🐡 callback 🍇🔢🍉 🍇 🍉
  🆕 🍇🍉
  🆕 ▶️ 🦈 🍼 depth 🔢 🍇 🍉
🍉

🏁 🍇
  🆕🐟❗️ ➡️ fish
  🏊 fish 5❗️ ➡️ 🖍🆕 total
  🍿 1 2 3 🍆 ➡️ list
  🍿 🔤a🔤 ➡️ 1 🍆 ➡️ dictionary
  🍨🐚🔢🍆 ➡️ empty
  🔂 item list 🍇
    total ⬅️ total ➕ item
  🍉
  ↪️ total ▶️🙌 10 🤝 👍 🍇
    😀 🔤Deep 😀 ❌n 🧲total🧲🔤❗️
  🍉
  🙅↪️ 👎 🍇 🍉
  🙅 🍇 ↩️↩️ 🍉
  🆗 value 🔨❗️ 🍇 🍉 🙅 error 🍇 🍉
  🍇 x 🔢 ➡️ 🔢 ↩️ x 🍉 ➡️ double
  ⁉️ double 2❗️ 💭 😀 in a comment
  💭🔜 🔂 in a
  long comment 🔚💭
  🎍🐌 ↪️ 👎 🍇 🍉
🍉
`;

/** Each case is a needle, found by its first occurrence or its n-th with "needle#n", and the expected section. */
const cases = [
    ['📘', 'packageDocumentation'], ['Fish and', 'packageDocumentation'], ['A fish', 'documentation'],
    ['🐇', 'defineClass'], ['🍇', 'typeBody'], ['🍉#6', 'typeBody'],
    ['🖍', 'instanceVariable'], ['🆕', 'instanceVariable'], ['⬅️', 'defaultValue'], ['depth', 'variable'],
    ['0', 'number'],
    ['❗️', 'declareMethod'], ['🏊', undefined], ['➡️', 'returnType'], ['🍇#2', 'methodBody'],
    ['⬅️➕', 'operatorAssign'], ['↩️', 'return'],
    ['🐇#2', 'typeMethod'], ['🍇#3', 'callableType'], ['🍇#4', 'methodBody'],
    ['🆕#2', 'declareInitializer'], ['🍇#5', 'initializerBody'], ['▶️', 'namedInitializer'], ['🍼', 'initParameter'],
    ['🏁', 'start'], ['🍇#7', 'start'],
    ['🆕#4', 'instantiate'], ['❗️#3', 'call'], ['➡️#2', 'constant'], ['🖍#2', 'mutable'], ['🆕#5', 'mutable'],
    ['🍿', 'list'], ['🍆', 'list'], ['➡️#5', 'dictionary'], ['🍿#2', 'dictionary'], ['🐚', 'generic'], ['🍆#3', 'generic'],
    ['🔂', 'forIn'], ['🍇#8', 'block'], ['⬅️ total', 'assign'], ['➕ item', 'operator'],
    ['↪️', 'if'], ['▶️🙌', 'operator'], ['🤝', 'shortCircuit'], ['👍', 'true'],
    // An emoji in a string is part of the string, and one in an interpolation is code again.
    ['😀', undefined], ['Deep', 'string'], ['😀#2', 'string'], ['❌n', 'escape'], ['🧲', 'interpolation'],
    ['total🧲', 'variable'], ['🔤Deep', 'string'],
    ['🙅↪️', 'elseIf'], ['↪️#2', 'elseIf'], ['🙅#2', 'else'], ['↩️↩️', 'returnNothing'],
    ['🆗', 'handle'], ['🙅#3', 'handleError'],
    ['🍇 x', 'closure'], ['➡️ 🔢 ↩️', 'closureReturnType'], ['⁉️', 'callCallable'],
    ['😀 in a', 'comment'], ['🔂 in a', 'comment'], ['long', 'comment'],
    ['🎍', 'branchSpeed'], ['🐌', 'branchSpeed'],
];

const document = new LearnerDocument(FISH);
let failures = 0;
for (const [needle, expected] of cases) {
    const [text, n] = needle.split('#');
    let index = -1;
    for (let i = 0; i < Number(n ?? 1); i++) index = FISH.indexOf(text, index + 1);
    assert.notStrictEqual(index, -1, `${needle} is not in the snippet`);
    const actual = document.explain(index)?.section;
    const name = actual && Object.keys(sections).find((key) => sections[key] === actual);
    if (name !== expected) {
        console.error(`${needle}: expected ${expected}, got ${name}`);
        failures++;
    }
}

// Every section links to a heading that the documentation has, if its source is next to this repository.
const docs = path.resolve(__dirname, '..', '..', '..', '..', 'emojicode.github.io', 'src');
if (fs.existsSync(docs)) {
    for (const [name, section] of Object.entries(sections)) {
        const [, book, page, anchor] = /^docs\/(\w+)\/([\w-]+)\.html(?:#(.*))?$/.exec(section.path);
        const markdown = fs.readFileSync(path.join(docs, book, `${page}.md`), 'utf8');
        // The documentation compiler makes the IDs of headings like this.
        const ids = [...markdown.matchAll(/^#+ (.+)$/gm)].map((m) => m[1].trim().toLowerCase().replace(/[^\w]+/g, '-'));
        if (anchor !== undefined && !ids.includes(anchor)) {
            console.error(`${name}: ${section.path} has no heading with the ID ${anchor}`);
            failures++;
        }
    }
}
else {
    console.log(`Skipped checking the anchors: ${docs} does not exist.`);
}

if (failures > 0) {
    process.exit(1);
}
console.log(`All ${cases.length} learner docs cases passed.`);
