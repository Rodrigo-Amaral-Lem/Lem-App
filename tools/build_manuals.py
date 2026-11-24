# SPDX-License-Identifier: GPL-3.0-only
"""Build both manuals from Markdown and the shared Typst template.

Requires Python 3.10+ and typst==0.15.2. Run from the App repository root.
Generated output defaults to ignored build/manuals, separate from source files.
The generated .typ files can also be edited/compiled directly with Typst.
"""
import argparse
import json
from pathlib import Path
import re
import shutil

APP = Path(__file__).resolve().parents[1]
SHOTS = {
    "android": {3: "catalogo-android", 4: "relatorio-android", 9: "revisao-android"},
    "desktop": {3: "catalogo-desktop", 4: "relatorio-navegacao-desktop", 9: "revisao-desktop"},
}
CAPTIONS = {3: "Contratos e carregadores", 4: "Navegação no relatório", 7: "Nome e assinatura do técnico", 9: "Revisão das pendências"}


def quoted(value):
    return json.dumps(value, ensure_ascii=False)


def inline(value):
    """Keep user text in quoted strings, never execute Markdown as Typst code."""
    result = []
    for part in re.split(r'(\*\*.*?\*\*|\[[^\]]+\]\([^)]+\))', value):
        if part.startswith('**') and part.endswith('**'):
            result.append('#strong(' + quoted(part[2:-2]) + ')')
        elif re.fullmatch(r'\[[^\]]+\]\([^)]+\)', part):
            label, target = re.fullmatch(r'\[([^\]]+)\]\(([^)]+)\)', part).groups()
            result.append('#link(' + quoted(target) + ')[' + '#text(' + quoted(label) + ')]')
        elif part:
            result.append('#text(' + quoted(part) + ')')
    return ''.join(result)


def convert(platform):
    lines = (APP / 'docs' / f'manual-utilizador-{platform}.md').read_text(encoding='utf-8').splitlines()
    title = lines[0].removeprefix('# ')
    edition = lines[2]
    start = next(i for i, line in enumerate(lines) if line.startswith('## '))
    audience = ''
    marker = '**Para utilizadores finais**'
    if marker in lines[:start]:
        audience_start = lines.index(marker) + 1
        audience = next((line.strip() for line in lines[audience_start:start] if line.strip()), '')
    body = ['#import "template.typ": manual, screenshot, step, note', '#show: manual.with(title: ' + quoted(title) + ', edition: ' + quoted(edition) + ', audience: ' + quoted(audience) + ')']
    section = 0

    def illustration():
        if section in SHOTS[platform]:
            name = SHOTS[platform][section]
            full_page = platform == 'android' and section == 4
            if full_page:
                body.append('#pagebreak(weak: true)')
            body.append('#screenshot(' + quoted('images/' + name + '.png') + ', ' + quoted(CAPTIONS[section] + ' - ' + platform + '. Dados fictícios.') + (', height: 190mm' if full_page else '') + ')')

    i = start
    while i < len(lines):
        line = lines[i].strip()
        if not line:
            body.append('')
        elif line.startswith('## '):
            illustration()
            section += 1
            body.extend(['#pagebreak(weak: true)', '#heading(level: 1)[' + inline(line[3:]) + ']'])
        elif line.startswith('### '):
            # Keep the desktop correction procedure together after the expanded
            # batch-export instructions instead of splitting its first paragraph.
            if ((section == 3 and line.startswith('### Apagar e recuperar pedidos'))
                    or (section == 15 and not line.startswith('### SEG '))
                    or (section == 5 and line.startswith('### Intensidade diferencial'))
                    or (section == 14 and line.startswith('### Verificação antes de'))
                    or (platform == 'desktop' and section == 12 and line.startswith('### Corrigir um relatório fechado'))):
                body.append('#pagebreak(weak: true)')
            body.append('#heading(level: 2)[' + inline(line[4:]) + ']')
        elif picture := re.fullmatch(r'!\[([^\]]+)\]\(manuals/(images/[a-z0-9-]+\.png)\)', line):
            caption, path = picture.groups()
            body.append('#screenshot(' + quoted(path) + ', ' + quoted(caption) + ', height: 120mm)')
        elif line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].strip().startswith('|'):
                rows.append([c.strip() for c in lines[i].strip().strip('|').split('|')])
                i += 1
            header, separator, *data = rows
            if not all(re.fullmatch(r':?-+:?', cell) for cell in separator):
                raise ValueError('Invalid table separator')
            columns = len(header)
            if any(len(row) != columns for row in data):
                raise ValueError('Inconsistent table width')
            widths = ['2.3fr', '1.7fr', '1.7fr'] if section == 15 and columns == 3 else ['1fr'] * columns
            if columns == 2:
                widths = ['1.05fr', '2.95fr'] if header[0] in ('Campos', 'Item', 'Grupo', 'Formato', 'Tipo ou limite', 'Medição') else ['1.35fr', '2.65fr']
            elif header == ['Ordem', 'Grupo', 'Conteúdo']:
                widths = ['0.5fr', '0.8fr', '2.7fr']
            body.append('#table(columns: (' + ', '.join(widths) + '),')
            body.append('table.header(repeat: true, ' + ', '.join('[' + inline(c) + ']' for c in header) + '),')
            body.extend(', '.join('[' + inline(c) + ']' for c in row) + ',' for row in data)
            body.append(')')
            continue
        elif re.match(r'^\d+\. ', line):
            number, text = line.split('. ', 1)
            body.append('#step(' + quoted(number + '.') + ')[' + inline(text) + ']')
        elif re.match(r'^\*\*(Fotografia obrigatória:|Pedido de suporte:|Regras comuns:|PDF sem rede:)', line):
            body.append('#note[' + inline(line) + ']')
        else:
            body.append(inline(line))
        i += 1
    illustration()
    if section != 16:
        raise ValueError(f'Expected 16 sections, found {section}')
    return '\n'.join(body) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=APP / 'build' / 'manuals')
    parser.add_argument('--publish', action='store_true', help='Also replace the two published PDFs in App/docs')
    args = parser.parse_args()
    import typst
    output = args.output.resolve()
    build_root = (APP / 'build').resolve()
    if output == APP or (APP in output.parents and build_root not in output.parents):
        parser.error('Within App, choose a subdirectory of build/ to keep generated files separate from source')
    output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(APP / 'docs/manuals/template.typ', output / 'template.typ')
    shutil.copytree(APP / 'docs/manuals/images', output / 'images', dirs_exist_ok=True)
    shutil.copyfile(APP / 'shared/branding/lem-logo-tight.svg', output / 'lem-logo-tight.svg')
    for platform in SHOTS:
        stem = f'manual-utilizador-{platform}'
        source = output / (stem + '.typ')
        source.write_text(convert(platform), encoding='utf-8')
        pdf = output / (stem + '.pdf')
        typst.compile(str(source), output=str(pdf), root=str(output))
        if args.publish:
            shutil.copyfile(pdf, APP / 'docs' / pdf.name)
        print(pdf)


if __name__ == '__main__':
    main()
