import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';

// -------------------------------------------------------------
// Helper to recursively collect files in a directory
// -------------------------------------------------------------
function getAllFiles(dir, fileList = []) {
  const files = readdirSync(dir);
  for (const file of files) {
    const filePath = join(dir, file);
    if (statSync(filePath).isDirectory()) {
      getAllFiles(filePath, fileList);
    } else if (file.endsWith('.ts') || file.endsWith('.tsx') || file.endsWith('.css')) {
      fileList.push(filePath);
    }
  }
  return fileList;
}

// -------------------------------------------------------------
// WCAG Relative Luminance & Contrast Ratio Calculator
// Formula: https://www.w3.org/WAI/GL/wiki/Relative_luminance
// -------------------------------------------------------------
function parseHexColor(hex) {
  const cleanHex = hex.replace('#', '');
  const r = parseInt(cleanHex.substring(0, 2), 16);
  const g = parseInt(cleanHex.substring(2, 4), 16);
  const b = parseInt(cleanHex.substring(4, 6), 16);
  return { r, g, b };
}

function getChannelLuminance(value8Bit) {
  const srgb = value8Bit / 255;
  return srgb <= 0.03928
    ? srgb / 12.92
    : Math.pow((srgb + 0.055) / 1.055, 2.4);
}

function getRelativeLuminance(hex) {
  const { r, g, b } = parseHexColor(hex);
  const rLum = getChannelLuminance(r);
  const gLum = getChannelLuminance(g);
  const bLum = getChannelLuminance(b);
  return 0.2126 * rLum + 0.7152 * gLum + 0.0722 * bLum;
}

function getContrastRatio(hex1, hex2) {
  const lum1 = getRelativeLuminance(hex1);
  const lum2 = getRelativeLuminance(hex2);
  const lighter = Math.max(lum1, lum2);
  const darker = Math.min(lum1, lum2);
  return (lighter + 0.05) / (darker + 0.05);
}

// =============================================================
// Test Suite: S4-E Design System Integration
// =============================================================

test('S4-DS-CONTRAST-16 & Task E-4: Mathematical WCAG AAA Contrast verification', () => {
  const colorBackground = '#07130E'; // Deep Forest Midnight OLED
  const colorText = '#F0FDF4';       // Text Primary
  const colorTextMuted = '#86EFAC';  // Text Muted (labels)

  const primaryContrast = getContrastRatio(colorText, colorBackground);
  const mutedContrast = getContrastRatio(colorTextMuted, colorBackground);

  // WCAG AAA requires >= 7.0:1 for normal text
  assert.ok(
    primaryContrast >= 7.0,
    `Primary text contrast ${primaryContrast.toFixed(2)}:1 must exceed WCAG AAA 7:1`
  );
  assert.ok(
    primaryContrast > 15.0,
    `Primary text contrast ${primaryContrast.toFixed(2)}:1 expected ~16.8:1`
  );

  assert.ok(
    mutedContrast >= 7.0,
    `Muted text contrast ${mutedContrast.toFixed(2)}:1 must exceed WCAG AAA 7:1`
  );
  assert.ok(
    mutedContrast > 8.0,
    `Muted text contrast ${mutedContrast.toFixed(2)}:1 expected ~8.2:1`
  );
});

test('S4-DS-COLOR-13 & Task E-1: 11 Core Tokens present in :root and ZERO literals outside :root', () => {
  const globalsCss = readFileSync('src/app/globals.css', 'utf8');

  // Verify all 11 core tokens exist
  const requiredTokens = [
    '--color-background',
    '--color-surface',
    '--color-surface-hover',
    '--color-border',
    '--color-primary',
    '--color-secondary',
    '--color-accent-amber',
    '--color-accent-indigo',
    '--color-danger',
    '--color-text',
    '--color-text-muted',
    '--color-text-subtle',
  ];

  for (const token of requiredTokens) {
    assert.ok(
      globalsCss.includes(token),
      `Missing required token ${token} in src/app/globals.css`
    );
  }

  // Extract :root block
  const rootMatch = globalsCss.match(/:root\s*\{([^}]+)\}/s);
  assert.ok(rootMatch, 'Could not locate :root block in globals.css');
  const rootContent = rootMatch[1];

  // Remove the :root block and check the remainder of the file
  const outsideRoot = globalsCss.replace(rootMatch[0], '');

  const colorLiteralRegex = /#[0-9A-Fa-f]{3,8}|rgba?\([^)]+\)/g;
  const matchesOutside = outsideRoot.match(colorLiteralRegex) || [];

  assert.equal(
    matchesOutside.length,
    0,
    `Zero Color Literal violation: Found color literals outside :root in globals.css: ${matchesOutside.join(', ')}`
  );
});

test('S4-DS-ICON-14 & Task E-3: Zero emoji across entire src/ codebase', () => {
  const files = getAllFiles('src');
  const emojiRegex = /[\u{1F300}-\u{1FAFF}\u{2600}-\u{26FF}\u{2700}-\u{27BF}]/u;

  for (const file of files) {
    const content = readFileSync(file, 'utf8');
    const match = content.match(emojiRegex);
    assert.equal(
      match,
      null,
      `Forbidden emoji found in ${file}: ${match ? match[0] : ''}`
    );
  }
});

test('S4-DS-TOUCH-15 & Task E-2: Mobile Touch Ergonomics on interactive elements', () => {
  const interactiveFiles = [
    'src/components/layout/Header.tsx',
    'src/components/layout/MobileActionBar.tsx',
    'src/components/dashboard/SeasonPanel.tsx',
    'src/components/dashboard/GroupCard.tsx',
    'src/components/dashboard/NodeCard.tsx',
    'src/components/dashboard/TreatmentPanel.tsx',
    'src/components/dashboard/MeasurementPanel.tsx',
    'src/components/common/WsBanner.tsx',
    'src/components/common/Modal.tsx',
  ];

  for (const file of interactiveFiles) {
    const content = readFileSync(file, 'utf8');
    assert.ok(
      content.includes('active:scale-95'),
      `${file} missing active:scale-95 micro-interaction`
    );
    assert.ok(
      content.includes('min-h-') || content.includes('btn-primary') || content.includes('btn-secondary'),
      `${file} missing minimum touch target height declaration`
    );
  }
});

test('S4-DS-MOBILE-17 & Task E-2: Mobile-First Grid & iOS Safe Area handling', () => {
  const layoutContent = readFileSync('src/app/dashboard/layout.tsx', 'utf8');
  const cssContent = readFileSync('src/app/globals.css', 'utf8');
  const mobileBarContent = readFileSync('src/components/layout/MobileActionBar.tsx', 'utf8');

  // Check iOS Safe Area usage
  assert.ok(
    layoutContent.includes('env(safe-area-inset-bottom)'),
    'dashboard/layout.tsx missing iOS safe-area-inset-bottom padding'
  );
  assert.ok(
    mobileBarContent.includes('env(safe-area-inset-bottom)'),
    'MobileActionBar.tsx missing safe-area-inset-bottom'
  );
  assert.ok(
    cssContent.includes('env(safe-area-inset-bottom)'),
    'globals.css missing safe area padding utility'
  );

  // Check responsive grid distribution in dashboard
  const groupGridContent = readFileSync('src/components/dashboard/GroupGrid.tsx', 'utf8');
  const nodeGridContent = readFileSync('src/components/dashboard/NodeGrid.tsx', 'utf8');
  const pageContent = readFileSync('src/app/dashboard/page.tsx', 'utf8');

  assert.ok(groupGridContent.includes('grid-cols-1'), 'GroupGrid missing mobile single column grid-cols-1');
  assert.ok(groupGridContent.includes('lg:grid-cols-4'), 'GroupGrid missing desktop 4-column layout lg:grid-cols-4');

  assert.ok(nodeGridContent.includes('grid-cols-1'), 'NodeGrid missing mobile single column grid-cols-1');
  assert.ok(nodeGridContent.includes('lg:grid-cols-4'), 'NodeGrid missing desktop 4-column layout lg:grid-cols-4');

  assert.ok(pageContent.includes('lg:grid-cols-2'), 'page.tsx bottom section missing 2-col layout lg:grid-cols-2');
});

test('S4-DS-FONT-12 & Cumulative Layout Shift prevention: JetBrains Mono + tabular-nums + min-height', () => {
  const globalsCss = readFileSync('src/app/globals.css', 'utf8');
  const nodeCardContent = readFileSync('src/components/dashboard/NodeCard.tsx', 'utf8');
  const groupCardContent = readFileSync('src/components/dashboard/GroupCard.tsx', 'utf8');

  // Font imports
  assert.ok(globalsCss.includes('JetBrains+Mono'), 'globals.css missing JetBrains Mono font import');
  assert.ok(globalsCss.includes('Outfit'), 'globals.css missing Outfit font import');
  assert.ok(globalsCss.includes('tabular-nums'), 'globals.css missing tabular-nums utility');

  // Fixed card heights prevent CLS
  assert.ok(nodeCardContent.includes('min-h-[220px]'), 'NodeCard missing min-h-[220px] to eliminate CLS');
  assert.ok(groupCardContent.includes('min-h-[220px]'), 'GroupCard missing min-h-[220px] to eliminate CLS');
});
