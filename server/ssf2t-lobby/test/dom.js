// Runs the dashboard script against a minimal DOM and a canned API, so the tests read what it renders.
import vm from 'node:vm';

import { ADMIN_HTML, ADMIN_JS } from '../src/admin_page.js';

class FakeNode {
  constructor(tag) {
    this.tag = tag;
    this.children = [];
    this.className = '';
    this.style = {};
    this.attrs = {};
    this.listeners = {};
    this.hidden = false;
    this.title = '';
    this.value = '';
    this.checked = false;
    this.own = '';
  }

  get textContent() {
    return this.children.length ? this.children.map((c) => c.textContent).join('') : this.own;
  }

  set textContent(value) {
    this.children = [];
    this.own = String(value);
  }

  get firstChild() {
    return this.children[0] ?? null;
  }

  appendChild(child) {
    this.children.push(child);
    return child;
  }

  removeChild(child) {
    this.children = this.children.filter((c) => c !== child);
  }

  setAttribute(name, value) {
    this.attrs[name] = value;
  }

  addEventListener(type, fn) {
    (this.listeners[type] ??= []).push(fn);
  }

  getBoundingClientRect() {
    return { left: 0, width: 900 };
  }

  focus() {}

  find(predicate, found = []) {
    if (predicate(this)) found.push(this);
    for (const child of this.children) child.find(predicate, found);
    return found;
  }

  rows() {
    return this.find((n) => n.tag === 'tr').map((tr) => tr.children.map((c) => c.textContent));
  }
}

export async function openDashboard({ summary, history, historyFor, range = '30', recent = '20' }) {
  const nodes = new Map();
  for (const id of ADMIN_HTML.matchAll(/id="([^"]+)"/g)) nodes.set(id[1], new FakeNode('div'));
  nodes.get('range').value = range;
  nodes.get('recent-limit').value = recent;
  const requests = [];
  const sandbox = {
    document: {
      hidden: false,
      getElementById: (id) => nodes.get(id) ?? null,
      createElement: (tag) => new FakeNode(tag),
      createElementNS: (ns, tag) => new FakeNode(tag),
      createTextNode: (text) => Object.assign(new FakeNode('#text'), { own: String(text) }),
    },
    window: { innerWidth: 1200, innerHeight: 800 },
    sessionStorage: { getItem: () => '', setItem() {}, removeItem() {} },
    setInterval: () => 1,
    clearInterval() {},
    fetch: async (path) => {
      requests.push(path);
      const body = path.startsWith('/admin/api/summary') ? summary : (historyFor ? historyFor(path) : history);
      return { status: 200, ok: true, json: async () => JSON.parse(JSON.stringify(body)) };
    },
    Intl,
    Math,
    Date,
    Number,
    JSON,
    Promise,
    Error,
  };
  vm.runInNewContext(ADMIN_JS, sandbox);
  nodes.get('token').value = 'dashboard-token-0123456789';
  for (const fn of nodes.get('login').listeners.submit) fn({ preventDefault() {} });
  for (let i = 0; i < 20; i++) await Promise.resolve();
  const text = (id) => nodes.get(id).textContent;
  return { nodes, requests, text, rows: (id) => nodes.get(id).rows(), all: () => [...nodes.values()].map((n) => n.textContent).join('\n') };
}
