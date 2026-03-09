import { readdir, readFile } from 'node:fs/promises'
import path from 'node:path'

const SRC_DIR = path.join(process.cwd(), 'src')
const IGNORE_DIRS = new Set(['__tests__'])

const METRIC_PATTERNS = {
	forLoops: /for\s*\(/g,
	whileLoops: /while\s*\(/g,
	bufferConcat: /Buffer\.concat\(/g,
	jsonParse: /JSON\.parse\(/g,
	jsonStringify: /JSON\.stringify\(/g,
	protoDecode: /proto\.[A-Za-z0-9_.]+\.decode\(/g,
	protoEncode: /proto\.[A-Za-z0-9_.]+\.encode\(/g,
	nativeRefs: /BAILEYS_NATIVE/g,
	cryptoPrimitives: /(createCipheriv|createDecipheriv|createHash|createHmac)\(/g
}

const count = (text, regex) => {
	const matches = text.match(regex)
	return matches ? matches.length : 0
}

const walk = async dir => {
	const out = []
	const entries = await readdir(dir, { withFileTypes: true })
	for (const entry of entries) {
		const full = path.join(dir, entry.name)
		if (entry.isDirectory()) {
			if (IGNORE_DIRS.has(entry.name)) {
				continue
			}

			out.push(...(await walk(full)))
			continue
		}

		if (entry.isFile() && entry.name.endsWith('.ts')) {
			out.push(full)
		}
	}

	return out
}

const getDomain = relPath => {
	const parts = relPath.split(path.sep)
	return parts.length > 1 ? parts[0] : '(root)'
}

const scoreFile = ({ relPath, lines, metrics, usesNative }) => {
	const isTypes = relPath.startsWith(`Types${path.sep}`)
	const isLargeStaticConstants = /(^|[/\\])(WAM|WABinary)[/\\]constants\.ts$/.test(relPath)

	if (isTypes || isLargeStaticConstants) {
		return 0
	}

	const loopScore = metrics.forLoops * 2 + metrics.whileLoops * 3
	const binaryScore = metrics.bufferConcat * 4
	const jsonScore = (metrics.jsonParse + metrics.jsonStringify) * 3
	const protoScore = metrics.protoDecode * 4 + metrics.protoEncode * 3
	const nativeGapBonus = usesNative ? 0 : 6
	const nativeIntegrationPenalty = usesNative ? 3 : 0
	const cryptoPrimitivePenalty = metrics.cryptoPrimitives > 0 ? 20 : 0
	const lineScore = Math.floor(lines / 120)

	const raw =
		loopScore +
		binaryScore +
		jsonScore +
		protoScore +
		nativeGapBonus +
		lineScore -
		nativeIntegrationPenalty -
		cryptoPrimitivePenalty
	return Math.max(0, raw)
}

const renderMarkdown = ({ generatedAt, files, domainSummary }) => {
	const topFiles = [...files].sort((a, b) => b.score - a.score).slice(0, 25)
	const rows = topFiles
		.map(
			file =>
				`| ${file.score} | \`${file.relPath.replace(/\\/g, '/')}\` | ${file.lines} | ${
					file.usesNative ? 'yes' : 'no'
				} | ${file.metrics.forLoops}/${file.metrics.whileLoops} | ${file.metrics.bufferConcat} | ${
					file.metrics.jsonParse + file.metrics.jsonStringify
				} | ${file.metrics.protoEncode + file.metrics.protoDecode} |`
		)
		.join('\n')

	const domains = [...domainSummary.values()]
		.sort((a, b) => b.files - a.files)
		.map(
			domain =>
				`| ${domain.domain} | ${domain.files} | ${domain.lines} | ${domain.nativeFiles} | ${domain.forLoops} | ${
					domain.bufferConcat
				} | ${domain.jsonOps} | ${domain.protoOps} |`
		)
		.join('\n')

	return `# TS Native Hotspot Audit

Generated: ${generatedAt}

## Scoring Model

- Higher score means better candidate for C++ addon migration.
- Score weights: loops, Buffer concatenation, JSON parse/stringify, protobuf encode/decode volume, and file size.
- Files under \`Types/\` are scored as \`0\` because they are declarations.
- Very large static constants files are down-weighted.

## Top Candidate Files

| Score | File | Lines | Native Guard | Loops (for/while) | Buffer.concat | JSON ops | Proto ops |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: |
${rows}

## Domain Summary

| Domain | Files | Lines | Files Using BAILEYS_NATIVE | For/While Loops | Buffer.concat | JSON ops | Proto ops |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
${domains}
`
}

const main = async () => {
	const files = await walk(SRC_DIR)
	const results = []
	const domainSummary = new Map()

	for (const absPath of files) {
		const relPath = path.relative(SRC_DIR, absPath)
		const text = await readFile(absPath, 'utf8')
		const lines = text.split(/\r?\n/).length
		const metrics = {
			forLoops: count(text, METRIC_PATTERNS.forLoops),
			whileLoops: count(text, METRIC_PATTERNS.whileLoops),
			bufferConcat: count(text, METRIC_PATTERNS.bufferConcat),
			jsonParse: count(text, METRIC_PATTERNS.jsonParse),
			jsonStringify: count(text, METRIC_PATTERNS.jsonStringify),
			protoDecode: count(text, METRIC_PATTERNS.protoDecode),
			protoEncode: count(text, METRIC_PATTERNS.protoEncode),
			nativeRefs: count(text, METRIC_PATTERNS.nativeRefs),
			cryptoPrimitives: count(text, METRIC_PATTERNS.cryptoPrimitives)
		}
		const usesNative = metrics.nativeRefs > 0
		const score = scoreFile({ relPath, lines, metrics, usesNative })

		results.push({
			relPath,
			lines,
			metrics,
			usesNative,
			score
		})

		const domain = getDomain(relPath)
		const current =
			domainSummary.get(domain) ||
			{
				domain,
				files: 0,
				lines: 0,
				nativeFiles: 0,
				forLoops: 0,
				whileLoops: 0,
				bufferConcat: 0,
				jsonOps: 0,
				protoOps: 0
			}

		current.files += 1
		current.lines += lines
		current.nativeFiles += usesNative ? 1 : 0
		current.forLoops += metrics.forLoops + metrics.whileLoops
		current.whileLoops += metrics.whileLoops
		current.bufferConcat += metrics.bufferConcat
		current.jsonOps += metrics.jsonParse + metrics.jsonStringify
		current.protoOps += metrics.protoDecode + metrics.protoEncode
		domainSummary.set(domain, current)
	}

	const report = renderMarkdown({
		generatedAt: new Date().toISOString(),
		files: results,
		domainSummary
	})

	if (process.argv.includes('--json')) {
		console.log(
			JSON.stringify(
				{
					generatedAt: new Date().toISOString(),
					files: results.sort((a, b) => b.score - a.score),
					domains: [...domainSummary.values()].sort((a, b) => b.files - a.files)
				},
				null,
				2
			)
		)
		return
	}

	console.log(report)
}

main().catch(error => {
	console.error(error)
	process.exitCode = 1
})
