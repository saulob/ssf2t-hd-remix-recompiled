// Internal character ids as the game's own name table (guest 0x83A21280, ids 0 to 16 in order) numbers them; the client
// reports ids, never text. 17 and 18 are this port's own secret identities, which the guest never numbers.
export const CHARACTERS = [
  'Ryu', 'E. Honda', 'Blanka', 'Guile', 'Ken', 'Chun-Li', 'Zangief', 'Dhalsim', 'M. Bison', 'Sagat', 'Balrog', 'Vega',
  'Cammy', 'T. Hawk', 'Fei Long', 'Dee Jay', 'Akuma', 'Evil Ryu', 'Violent Ken',
];

// Ids that need both players' consent. Each key also needs a bit pair in protocol.js SECRET_BITS.
export const SECRET_CHARACTER = { 16: 'akuma', 17: 'evil_ryu', 18: 'violent_ken' };
export const SECRET_KEYS = Object.values(SECRET_CHARACTER);

export const isCharacterId = (value) => Number.isInteger(value) && value >= 0 && value < CHARACTERS.length;

export const characterName = (id) => CHARACTERS[id] ?? `Unknown ${id}`;
