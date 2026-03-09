# WAProto Extractor

`proto-extract/` is a standalone maintenance utility used to rebuild `WAProto/WAProto.proto` from the current WhatsApp Web client bundle structure.

It is not part of the runtime package path. It exists so maintainers can refresh protobuf definitions in a repeatable way from a source checkout.

## What the Extractor Does

`index.js` performs this workflow:

1. Requests `https://web.whatsapp.com/sw.js`
2. Extracts the current client revision and the bootstrap script URL
3. Downloads the referenced WhatsApp Web JavaScript bundle
4. Parses that bundle with `acorn` and `acorn-walk`
5. Locates protobuf-like internal specifications
6. Reconstructs message and enum definitions
7. Writes the derived schema to `../WAProto/WAProto.proto`

The script does not update the TypeScript or C++ protobuf runtimes by itself. Those are separate follow-up steps.

## Local Usage

From the repository root:

```bash
yarn --cwd proto-extract install
yarn --cwd proto-extract start
```

Or from inside the directory:

```bash
npm install
npm start
```

## After a Successful Extraction

Review the resulting schema diff first. If the change is intentional:

```bash
yarn gen:protobuf
node scripts/generate-waproto-cpp.mjs
yarn verify:native-deps
```

Those follow-up commands refresh the TypeScript protobuf artifacts, refresh the native C++ protobuf artifacts, and revalidate the dependency manifest.

## Dependencies

The extractor currently uses:

- `acorn`
- `acorn-walk`
- `request`
- `request-promise-core`
- `request-promise-native`

These dependencies are isolated to `proto-extract/package.json`.

## Review and Governance Expectations

- Treat the generated schema as a reviewed derived artifact, not as blindly trusted source material.
- Do not commit downloaded WhatsApp Web JavaScript assets into this repository.
- Keep the repository description factual: this tool helps maintain interoperability of the fork; it does not imply affiliation with WhatsApp or Meta.
- If extraction fails or produces ambiguous output, prefer no update over an unverified schema change.
