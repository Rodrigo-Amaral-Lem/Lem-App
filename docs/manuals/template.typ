// SPDX-License-Identifier: GPL-3.0-only
// Visual presentation only; wording remains in the editable Markdown manuals.
#let brand = rgb("007f91")
#let ink = rgb("24343b")
#let muted = rgb("52616a")
#let rule = rgb("d5e3e6")
#let tint = rgb("eef7f8")

#let manual(title: "", edition: "", audience: "", body) = {
  set document(title: title, author: "LEM")
  set text(font: "Arial", size: 11pt, fill: ink, lang: "pt", region: "PT", hyphenate: false)
  set page(paper: "a4", margin: (x: 20mm, top: 21mm, bottom: 20mm),
    header: context {
      if counter(page).get().first() > 2 {
        let sections = query(heading.where(level: 1)).filter(h => h.location().page() <= here().page())
        if sections.len() > 0 {
          text(size: 8pt, fill: muted, sections.last().body)
          v(3pt)
          line(length: 100%, stroke: 0.5pt + rule)
        }
      }
    },
    footer: context {
      line(length: 100%, stroke: 0.5pt + rule)
      v(4pt)
      text(size: 8pt, fill: muted)[LEM · Manual de utilização #h(1fr) #counter(page).display("1 / 1", both: true)]
    })
  set par(justify: false, leading: 0.55em, spacing: 0.8em)
  set heading(numbering: none)
  show heading.where(level: 1): it => block(above: 5pt, below: 14pt, sticky: true)[
    #text(size: 23pt, weight: "bold", fill: brand)[#it.body]
    #v(7pt)
    #line(length: 22mm, stroke: 2pt + brand)
  ]
  show heading.where(level: 2): it => block(above: 12pt, below: 7pt, sticky: true)[
    #text(size: 12pt, weight: "bold", fill: ink)[#it.body]
  ]
  show table: set text(size: 10pt)
  set table(inset: (x: 7pt, y: 6pt),
    stroke: (x, y) => (bottom: 0.4pt + rule),
    fill: (x, y) => if y == 0 { rgb("deeff2") } else if calc.rem(y, 2) == 0 { rgb("f5f8f9") } else { none })
  show table.cell.where(y: 0): set text(weight: "bold", fill: ink)
  show figure.caption: set text(size: 9pt, fill: muted)
  show outline.entry: it => block(width: 100%, inset: (y: 4pt), breakable: false)[#it]

  image("lem-logo-tight.svg", width: 26mm)
  v(27mm)
  line(length: 27mm, stroke: 3pt + brand)
  v(10mm)
  text(size: 31pt, weight: "bold", fill: brand)[#title]
  v(10mm)
  block(width: 100%, fill: tint, inset: 12pt, radius: 3pt)[#text(size: 10pt)[#edition]]
  v(12mm)
  [Registar intervenções de manutenção, reunir evidências e entregar relatórios completos.]
  v(10mm)
  [*Para utilizadores finais*]
  if audience != "" {
    v(3mm)
    text(audience)
  }
  pagebreak()
  outline(title: [Índice], depth: 1)
  body
}

#let step(number, body) = block(above: 5pt, below: 6pt, breakable: false)[
  #grid(columns: (23pt, 1fr), gutter: 8pt,
    box(width: 23pt, inset: (y: 4pt), radius: 3pt, fill: tint)[#align(center)[#text(weight: "bold", fill: brand, number)]],
    body)
]

#let note(body) = block(width: 100%, inset: (x: 10pt, y: 8pt),
  fill: tint, stroke: (left: 2pt + brand), above: 7pt, below: 7pt)[#body]

#let screenshot(path, caption, height: 110mm) = figure(
  block(inset: 6pt, stroke: 0.5pt + rule, radius: 3pt)[
    #image(path, width: 100%, height: height, fit: "contain")
  ],
  caption: caption,
  supplement: [Figura],
)
