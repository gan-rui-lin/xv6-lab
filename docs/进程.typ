#import "lib.typ" : *

#import "@preview/cmarker:0.1.7"

#cmarker.render(
  read("进程.md"),
  math: mitex,
  scope: (image: (source, alt: none, format: auto) => image(source, alt: alt, format: format))  
)