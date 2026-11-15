# Editar e gerar os manuais

Os dois manuais usam **Markdown para o conteúdo e Typst para a composição dos PDFs**.
Typst é uma alternativa ao LaTeX. O modelo, o logótipo e as capturas ficam no repositório;
instale a fonte Arial no sistema. Não é necessário ter a aplicação Lem aberta para gerar os documentos.

## Onde editar

| Pretende alterar | Ficheiro |
| --- | --- |
| Texto desktop | [manual-utilizador-desktop.md](../manual-utilizador-desktop.md) |
| Texto Android | [manual-utilizador-android.md](../manual-utilizador-android.md) |
| Margens, tipos de letra, cores, capa, índice e rodapé | [template.typ](template.typ) |
| Ilustrações | PNGs em `images/`; associação às secções em [build_manuals.py](../../tools/build_manuals.py) |

Edite os ficheiros em UTF-8. No Markdown, `##` inicia uma das 15 secções, `###` cria um
subtítulo, `**texto**` marca negrito e as tabelas usam barras `|`. O conversor suporta
estes elementos, parágrafos, ligações e passos numerados; não é um conversor Markdown genérico.
Mantenha os números explícitos dos passos. Se acrescentar secções, ajuste também as associações
das figuras e a verificação do número de secções no gerador.

As ilustrações mostram dados fictícios. As capturas Android foram obtidas num emulador;
as da edição desktop mostram a interface desktop. Substitua apenas por capturas da plataforma
correta e sem dados pessoais. O logótipo vem de `shared/branding/lem-logo.png`.

## Gerar os dois PDFs

Requisitos: Python 3.10 ou posterior, Arial e o pacote `typst==0.15.2`. A geração é local,
sem serviço externo. Execute os exemplos a partir da **raiz do repositório App**, onde existem
`app/`, `docs/`, `shared/` e `tools/`. O ambiente `.venv/` e a saída `build/manuals/` são ignorados pelo Git.

```powershell
python -m venv .venv
./.venv/Scripts/python.exe -m pip install typst==0.15.2
./.venv/Scripts/python.exe tools/build_manuals.py
```

Em Bash, após criar o ambiente, utilize `.venv/bin/python` nos mesmos comandos.
O diretório `build/manuals/` contém os dois PDFs, dois documentos `.typ`, o modelo, o logótipo e as
imagens. O script substitui estes ficheiros na saída em cada execução. Não guarda alterações
feitas diretamente nesses ficheiros gerados: faça alterações permanentes nos Markdown,
no modelo e nas imagens do repositório.

Também pode copiar toda a pasta gerada para outro local, editar os `.typ` diretamente num
editor Typst e compilá-los nesse local. Nessa modalidade a cópia é independente e as alterações
não regressam automaticamente aos Markdown. É necessário conservar o modelo, o logótipo e
a subpasta `images` junto dos `.typ`.

Pode escolher outra subpasta de `build/` com `--output build/manuals-preview`. O gerador recusa
usar as pastas de código ou documentação como destino temporário. O texto apresentado sob
**Para utilizadores finais** no Markdown também é utilizado na capa de cada PDF.

Para atualizar também os PDFs publicados em `docs/`, depois de os rever:

```powershell
./.venv/Scripts/python.exe tools/build_manuals.py --publish
```

`--publish` substitui apenas os dois PDFs publicados. Não cria commits. O gerador não modifica
o texto dos manuais. O índice, as ligações internas, as legendas e a paginação são automáticos;
o número de páginas pode mudar quando altera o texto.

## Rever alterações

Compare instruções comuns nas duas edições com o [contrato do relatório](../../../Common/docs/report-contract.md),
o [fluxo de trabalho](../field-workflow.md) e os [limites e proteção de dados](../../../Common/docs/security.md).
Confirme comportamentos na implementação quando os documentos divergirem. Preserve as diferenças de
plataforma: câmara/galeria no Android e importação da fotografia de assinatura no desktop.

Depois de gerar, abra ambos os PDFs e reveja tabelas, imagens, índice, acentos, símbolos e quebras
de página. Uma compilação sem erro não prova que as instruções correspondem ao comportamento da app.

A secção 15 contém a referência de todos os campos. Cada grupo (ou parte de grupo) começa numa página nova. Ao alterar o formulário, reveja também esta referência nas duas edições, incluindo os requisitos de N/A e fotografia.
