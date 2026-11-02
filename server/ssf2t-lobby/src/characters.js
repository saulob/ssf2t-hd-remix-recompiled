// Internal character ids as the game's own name table (guest 0x83A21280, ids 0 to 16 in order) numbers them; the client
// reports ids, never text.
export const CHARACTERS = [
  'Ryu', 'E. Honda', 'Blanka', 'Guile', 'Ken', 'Chun-Li', 'Zangief', 'Dhalsim', 'M. Bison', 'Sagat', 'Balrog', 'Vega',
  'Cammy', 'T. Hawk', 'Fei Long', 'Dee Jay', 'Akuma',
];

export const isCharacterId = (value) => Number.isInteger(value) && value >= 0 && value < CHARACTERS.length;

export const characterName = (id) => CHARACTERS[id] ?? `Unknown ${id}`;
