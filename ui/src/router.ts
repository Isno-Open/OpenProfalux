// Un routeur minimal par le hash de l'adresse, comme l'interface d'aujourd'hui : #onglet/section/suite.
// Les anciennes adresses #wifi, #mqtt et #cloud (dont celle du portail captif, /#wifi) menent a leur section
// de Reseau.
import { reactive } from 'vue';
import { SECTION, TAB, type SectionId, type TabId } from './domain';
import { TABS, type Tab } from './nav';

const ALIASES: Record<string, [TabId, SectionId]> = {
  [SECTION.wifi]: [TAB.network, SECTION.wifi],
  [SECTION.mqtt]: [TAB.network, SECTION.mqtt],
  [SECTION.cloud]: [TAB.network, SECTION.cloud],
};

interface Route { tab: TabId; section: SectionId | null; rest: string[] }
export const route = reactive<Route>({ tab: TAB.control, section: null, rest: [] });

function read(): void {
  const parts = (location.hash || `#${TAB.control}`).slice(1).split('/');
  let tabId: string = parts[0] ?? '';
  let sectionId: string | undefined = parts[1];
  const alias = ALIASES[tabId];
  if (alias) [tabId, sectionId] = alias;
  const fallback: Tab | undefined = TABS[0];
  const tab = TABS.find(t => t.id === tabId) ?? fallback;
  if (!tab) return;
  const sections = tab.sections ?? [];
  const section = sections.find(x => x.id === sectionId);
  route.tab = tab.id;
  route.section = section ? section.id : (sections[0]?.id ?? null);
  route.rest = section ? parts.slice(2) : [];
}

export function navigate(path: string): void {
  if (location.hash.slice(1) === path) read();
  else location.hash = path;
}

addEventListener('hashchange', read);
read();
