import { execFile } from 'node:child_process';
import path from 'node:path';
import { codexHome } from './monitor';

interface Cache {
  at: number;
  map: Map<string, boolean>;
  ok: boolean;
}

let cache: Cache = { at: 0, map: new Map(), ok: false };

export async function getCodexUnread(): Promise<{ map: Map<string, boolean>; ok: boolean }> {
  if (Date.now() - cache.at < 2000) return { map: cache.map, ok: cache.ok };
  const db = path.join(codexHome(), 'state_5.sqlite');
  const script = `const {DatabaseSync}=require('node:sqlite');const db=new DatabaseSync(${JSON.stringify(
    db,
  )},{readOnly:true});const rows=db.prepare('select thread_id,first_unread_turn from thread_read_receipts').all();console.log(JSON.stringify(rows));db.close();`;
  const result = await new Promise<Cache>((resolve) => {
    execFile(
      process.execPath,
      ['--experimental-sqlite', '-e', script],
      {
        windowsHide: true,
        timeout: 5000,
        env: { ...process.env, ELECTRON_RUN_AS_NODE: '1' },
      },
      (err, stdout) => {
        if (err) {
          resolve({ at: Date.now(), map: new Map(), ok: false });
          return;
        }
        try {
          const start = stdout.indexOf('[');
          const end = stdout.lastIndexOf(']');
          const rows = JSON.parse(stdout.slice(start, end + 1)) as Array<{
            thread_id: string;
            first_unread_turn: string | null;
          }>;
          const map = new Map<string, boolean>();
          for (const row of rows) map.set(row.thread_id, Boolean(row.first_unread_turn));
          resolve({ at: Date.now(), map, ok: true });
        } catch {
          resolve({ at: Date.now(), map: new Map(), ok: false });
        }
      },
    );
  });
  cache = result;
  return { map: result.map, ok: result.ok };
}
