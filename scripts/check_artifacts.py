"""Structural checks complement visual PDF review."""
from pathlib import Path
from html.parser import HTMLParser
from pypdf import PdfReader
from reportlab.pdfbase.ttfonts import TTFont
import json
root=Path(__file__).resolve().parents[1]
pages=PdfReader(root/'output/report.pdf').pages
texts=[page.extract_text() for page in pages]
assert len(pages)==26
assert all(len(text)>100 and 'АВТ-412' in text for text in texts)
assert all(str(i+1) in text for i,text in enumerate(texts))
class Images(HTMLParser):
    paths=[]
    def handle_starttag(self,tag,attrs):
        if tag=='img': self.paths.append(dict(attrs)['src'])
parser=Images()
parser.feed((root/'output/report.html').read_text(encoding='utf-8'))
assert all((root/'output'/path).is_file() for path in parser.paths)
assert len(list((root/'output/charts').glob('*.png')))==6
assert len(list((root/'output/charts').glob('*.svg')))==6
fonts=[TTFont('a','C:/Windows/Fonts/arial.ttf'),TTFont('m','C:/Windows/Fonts/consola.ttf')]
missing={ord(c) for text in texts for c in text if ord(c)>127
         and not any(ord(c) in font.face.charToGlyph for font in fonts)}
assert not missing, [hex(x) for x in missing]
result={'pdf_pages':len(pages),'html_images':len(parser.paths),
        'chart_pairs_png_svg':6,'missing_glyphs':[], 'status':'PASS'}
(root/'results/artifact_checks.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
