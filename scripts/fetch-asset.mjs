import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { readdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const getArg = (name, fallback) => {
  const i = args.indexOf(`--${name}`);
  return i >= 0 ? args[i + 1] : fallback;
};

const id = getArg('id', 'char_1015_aglna2');
const assetBase = 'https://torappu.prts.wiki/assets/char_spine';
const operatorDir = path.join(root, 'public/assets/operators', id);

async function fetchOk(url) {
  const res = await fetch(url, {
    headers: {
      'User-Agent':
        'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0 Safari/537.36',
    },
  });
  if (!res.ok) throw new Error(`${res.status} ${url}`);
  return Buffer.from(await res.arrayBuffer());
}

async function download(url, file) {
  const buf = await fetchOk(url);
  await mkdir(path.dirname(file), { recursive: true });
  await writeFile(file, buf);
  console.log(`  ${path.relative(root, file)} (${buf.length} bytes)`);
}

async function main() {
  console.log(`fetching ${id}`);
  const metaBuf = await fetchOk(`${assetBase}/${id}/meta.json`);
  const meta = JSON.parse(metaBuf.toString('utf8'));
  await mkdir(operatorDir, { recursive: true });
  await writeFile(path.join(operatorDir, 'meta.json'), JSON.stringify(meta, null, 2));

  const skinName = Object.keys(meta.skin)[0];
  const skin = meta.skin[skinName];
  const targets = [
    ['build', skin['基建']?.file],
    ['front', skin['正面']?.file],
  ];
  for (const [kind, base] of targets) {
    if (!base) {
      console.warn(`skin ${skinName} has no ${kind} model, skipping`);
      continue;
    }
    const name = path.basename(base);
    const dir = path.join(operatorDir, 'spine', kind);
    for (const ext of ['.skel', '.atlas', '.png']) {
      await download(`${assetBase}/${id}/${base}${ext}`, path.join(dir, `${name}${ext}`));
    }
  }

  const avatarUrl =
    'https://media.prts.wiki/c/ca/%E5%A4%B4%E5%83%8F_%E5%AE%89%E6%B4%81%E8%8E%89%E5%A8%9C.png';
  if (id === 'char_1015_aglna2') {
    await download(avatarUrl, path.join(operatorDir, 'avatar.png'));
  }

  // Sanity-check the animation names we rely on.
  const want = [
    'Relax',
    'Sit',
    'Sleep',
    'Move',
    'Interact',
    'Idle',
    'Attack',
    'Start',
    'Start_2',
    'Skill_2_Begin',
    'Skill_3_Begin',
    'Skill_3_Restart_Begin',
    'Die',
    'Skill_1_Die',
    'Skill_1_End',
    'Skill_1_Idle',
    'Skill_1_Loop',
    'Skill_2_Die',
    'Skill_2_End',
    'Skill_2_Idle',
    'Skill_2_Loop',
    'Skill_2_Begin',
    'Skill_2_Takeoff_Begin',
    'Skill_2_Takeoff_End',
    'Skill_2_Takeoff_Loop',
    'Skill_3_Die',
    'Skill_3_End',
    'Skill_3_Idle',
    'Skill_3_Loop',
    'Skill_3_Combat',
    'Skill_3_Move',
  ];
  const text = [];
  for (const kind of ['build', 'front']) {
    try {
      const files = readdirSync(path.join(operatorDir, 'spine', kind));
      for (const f of files.filter((x) => x.endsWith('.skel'))) {
        const buf = await readFile(path.join(operatorDir, 'spine', kind, f));
        text.push(buf.toString('latin1'));
      }
    } catch {}
  }
  const all = text.join('\n');
  const missing = want.filter((n) => !all.includes(n));
  if (missing.length) console.warn('missing expected animation names:', missing.join(', '));
  else console.log('all expected animation names present');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
