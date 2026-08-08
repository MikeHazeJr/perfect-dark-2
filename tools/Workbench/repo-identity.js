#!/usr/bin/env node
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');

function realpathIfPresent(value) {
  const resolved = path.resolve(value);
  try {
    return fs.realpathSync.native(resolved);
  } catch (_) {
    return resolved;
  }
}

function samePath(left, right) {
  const a = realpathIfPresent(left).replace(/[\\/]+$/, '');
  const b = realpathIfPresent(right).replace(/[\\/]+$/, '');
  return process.platform === 'win32' ? a.toLowerCase() === b.toLowerCase() : a === b;
}

function normalizeGitPath(value, base) {
  let text = String(value || '').trim().replace(/^"|"$/g, '');
  const homePrefix = /^\/home\/[^/]+\/(.*)$/.exec(text);
  const drivePrefix = /^\/([A-Za-z])\/(.*)$/.exec(text);
  if (homePrefix) text = path.join(os.homedir(), homePrefix[1]);
  else if (drivePrefix) text = `${drivePrefix[1]}:\\${drivePrefix[2]}`;
  return realpathIfPresent(path.isAbsolute(text) || /^[A-Za-z]:[\\/]/.test(text)
    ? text : path.resolve(base, text));
}

function readRef(commonDir, gitDir, ref) {
  for (const root of [gitDir, commonDir]) {
    const file = path.join(root, ...ref.split('/'));
    if (fs.existsSync(file)) return fs.readFileSync(file, 'utf8').trim();
  }
  const packed = path.join(commonDir, 'packed-refs');
  if (fs.existsSync(packed)) {
    for (const line of fs.readFileSync(packed, 'utf8').split(/\r?\n/)) {
      if (!line || line.startsWith('#') || line.startsWith('^')) continue;
      const [sha, name] = line.split(' ');
      if (name === ref) return sha;
    }
  }
  return null;
}

function readHead(gitDir, commonDir) {
  const headPath = path.join(gitDir, 'HEAD');
  if (!fs.existsSync(headPath)) return { branch: null, head: null };
  const value = fs.readFileSync(headPath, 'utf8').trim();
  const match = /^ref:\s*(.+)$/.exec(value);
  if (!match) return { branch: null, head: value || null };
  const ref = match[1];
  return {
    branch: ref.startsWith('refs/heads/') ? ref.slice('refs/heads/'.length) : ref,
    head: readRef(commonDir, gitDir, ref),
  };
}

function resolveRepoIdentity(worktreeRoot) {
  const resolvedWorktree = realpathIfPresent(worktreeRoot);
  const dotGit = path.join(resolvedWorktree, '.git');
  if (!fs.existsSync(dotGit)) {
    throw new Error(`Workbench requires a Git worktree: ${resolvedWorktree}`);
  }

  let gitDir;
  if (fs.statSync(dotGit).isDirectory()) {
    gitDir = realpathIfPresent(dotGit);
  } else {
    const match = /^gitdir:\s*(.+)\s*$/im.exec(fs.readFileSync(dotGit, 'utf8'));
    if (!match) throw new Error(`Invalid linked-worktree .git file: ${dotGit}`);
    gitDir = normalizeGitPath(match[1], resolvedWorktree);
  }

  const commonFile = path.join(gitDir, 'commondir');
  const commonDir = fs.existsSync(commonFile)
    ? normalizeGitPath(fs.readFileSync(commonFile, 'utf8'), gitDir)
    : gitDir;
  if (path.basename(commonDir).toLowerCase() !== '.git') {
    throw new Error(`Unsupported Git common directory: ${commonDir}`);
  }

  const projectRoot = realpathIfPresent(path.dirname(commonDir));
  const current = readHead(gitDir, commonDir);
  const canonical = readHead(commonDir, commonDir);
  return {
    projectRoot,
    worktreeRoot: resolvedWorktree,
    gitCommonDir: commonDir,
    gitDir,
    branch: current.branch,
    head: current.head,
    canonicalBranch: canonical.branch,
    canonicalHead: canonical.head,
    canonical: samePath(projectRoot, resolvedWorktree),
  };
}

function validateStartupIdentity(identity, options) {
  const isolated = Boolean(options && options.isolated);
  const port = Number(options && options.port);
  const hasDataOverride = Boolean(options && options.hasDataOverride);
  if (!isolated && !identity.canonical) {
    throw new Error(
      `Refusing noncanonical Workbench startup from ${identity.worktreeRoot}. ` +
      `Canonical project root is ${identity.projectRoot}. ` +
      'Use the canonical checkout, or set WORKBENCH_ISOLATED=1 with a nondefault port and WORKBENCH_DATA_DIR.'
    );
  }
  if (!isolated && hasDataOverride) {
    throw new Error('WORKBENCH_DATA_DIR requires explicit WORKBENCH_ISOLATED=1.');
  }
  if (isolated && !hasDataOverride) {
    throw new Error('Isolated Workbench startup requires WORKBENCH_DATA_DIR.');
  }
  if (isolated && port === 8378) {
    throw new Error('Isolated Workbench startup must use a nondefault WORKBENCH_PORT.');
  }
}

module.exports = {
  normalizeGitPath,
  resolveRepoIdentity,
  samePath,
  validateStartupIdentity,
};
