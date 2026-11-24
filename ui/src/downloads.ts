// Telecharger un fichier : sur le boitier, son lien direct (/api/backup, /api/frames, /api/log), que le cookie
// de session suit ; avec les fausses donnees, un fichier fabrique ici.
import api from './api/index';
import type { DownloadKey } from './domain';

export async function download(key: DownloadKey, fileName: string, build: () => string | Promise<string>): Promise<void> {
  const a = document.createElement('a');
  const link = api.downloadLinks ? api.downloadLinks[key] : undefined;
  a.href = link ?? URL.createObjectURL(new Blob([await build()], { type: 'application/octet-stream' }));
  a.download = fileName;
  document.body.appendChild(a);
  a.click();
  a.remove();
  if (a.href.startsWith('blob:')) setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}
