// La configuration du boitier (/api/config), pour les ecrans qui l'enregistrent (Wi-Fi, MQTT, Systeme › Appareil).
// Tant qu'elle n'est pas lue, rien ne doit partir : le formulaire est encore vide, et le firmware enregistre tel quel
// un champ vide (reseau Wi-Fi, broker ou nom effaces). Une lecture en echec se dit, et se refait.
import { onMounted, ref, type Ref } from 'vue';
import api from './api/index';
import type { Config } from './api/types';

export interface ConfigLoader {
  config: Ref<Config | null>;   // null tant qu'elle n'est pas lue : les boutons Enregistrer restent grises
  failed: Ref<boolean>;         // la derniere lecture a echoue
  load: () => Promise<void>;
}

/* Lit la config a l'ouverture de l'ecran et la passe a apply, qui remplit le formulaire. */
export function useConfig(apply: (c: Config) => void): ConfigLoader {
  const config = ref<Config | null>(null);
  const failed = ref(false);
  async function load(): Promise<void> {
    const c = await api.config().catch(() => null);
    failed.value = !c;
    if (!c) return;
    config.value = c;
    apply(c);
  }
  onMounted(load);
  return { config, failed, load };
}
