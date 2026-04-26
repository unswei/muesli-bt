# docs style guide

Use this guide when adding or changing user-facing documentation.

## headings

Use lowercase headings except proper nouns, API names, and acronyms.

## status vocabulary

Use these labels consistently:

- `released`: documented, tested, and part of the supported release surface.
- `experimental`: implemented but not yet stable enough for strong external dependence.
- `contract-only`: documented boundary or schema, but not yet a complete executable implementation.
- `planned`: roadmap item, not released functionality.

Future-facing pages must show a visible status block near the top.

## user-facing pages

Every user-facing page should start with:

- what this is;
- when to use it;
- released versus experimental status, if relevant.

## tutorials

Every tutorial should include:

- prerequisites;
- exact commands;
- expected output;
- expected artefacts;
- troubleshooting or failure interpretation where useful;
- next pages.

## API and reference pages

Every API/reference page should include:

- status;
- signature or schema;
- arguments;
- result;
- failure modes;
- example;
- related pages.

## benchmark and evidence pages

Every benchmark/evidence page should include:

- hardware;
- commit;
- build flags;
- command;
- raw output location;
- interpretation limits.

## roadmap pages

Roadmap pages must not describe planned functionality as released functionality.

## Australian English

Use Australian/British English in prose where practical. Keep external API names and standard terms unchanged where changing spelling would be confusing.
