const test = require('node:test');
const assert = require('node:assert/strict');
globalThis.window = globalThis;
require('../game-minigames.js');
const {petanqueNext} = globalThis.GameMinigames;

test('petanque: the side without the point throws until it has no boules, then the holder', () => {
  assert.equal(petanqueNext('you', {you: 2, npc: 1}), 'npc');
  assert.equal(petanqueNext('you', {you: 2, npc: 0}), 'you');
  assert.equal(petanqueNext('npc', {you: 1, npc: 2}), 'you');
  assert.equal(petanqueNext('npc', {you: 0, npc: 0}), null);// the mène is over
});

test('petanque: with no boule left in the court whoever has boules plays on (it used to freeze)', () => {
  assert.equal(petanqueNext(null, {you: 1, npc: 2}), 'you');
  assert.equal(petanqueNext(null, {you: 0, npc: 2}), 'npc');
  assert.equal(petanqueNext(null, {you: 0, npc: 0}), null);
});
