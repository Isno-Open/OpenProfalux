// Telecharger un fichier : sur le boitier, son lien direct (/api/backup, /api/frames, /api/log), que le
// cookie de session suit ; avec les fausses donnees, un fichier fabrique ici.
import api from './api/index.js';

export async function telecharger(cle, nom, fabriquer) {
  const a = document.createElement('a');
  if (api.liens && api.liens[cle]) a.href = api.liens[cle];
  else a.href = URL.createObjectURL(new Blob([await fabriquer()], { type: 'application/octet-stream' }));
  a.download = nom;
  document.body.appendChild(a);
  a.click();
  a.remove();
  if (a.href.startsWith('blob:')) setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}
