"""Make browser views of the actual recorded stdout, without changing any values."""
from pathlib import Path
from html import escape
root=Path(__file__).resolve().parents[1]
text=(root/'results/console.txt').read_text(encoding='utf-8-sig')
markers=['ТЕСТИРОВАНИЕ POSIX','ТЕСТИРОВАНИЕ std::thread','ОСНОВНОЕ ВЫЧИСЛЕНИЕ','ТОЧНОСТЬ И ШАГ']
starts=[text.index(x) for x in markers]+[len(text)]
directory=root/'output/demo'
directory.mkdir(parents=True,exist_ok=True)
for i in range(4):
    body=text[starts[i]:starts[i+1]]
    page='''<!doctype html><html lang="ru"><meta charset="utf-8"><title>Реальный вывод L1pp</title>
<style>*{box-sizing:border-box}body{margin:0;background:#0f172a;color:#e2e8f0;padding:28px 36px;font:17px Arial}
h1{font-size:23px;margin:0 0 10px}p{color:#94a3b8;font-size:14px;margin:5px 0}
pre{font:12px/1.45 Consolas,monospace;background:#172238;padding:14px;border:1px solid #334155;border-radius:10px;white-space:pre-wrap;overflow:auto;margin-top:20px}
.tag{color:#67e8f9}a{color:#67e8f9}</style>
<h1>L1pp · Протокол реального запуска</h1><p class="tag">Release x64 · N = 20 000 000 · потоки = 8 · максимум = 16 · повторов = 7</p>
<p>Содержимое results/console.txt; представление stdout в браузере.</p>'''
    page+='<pre>'+escape(body.rstrip())+'</pre>'
    page+='<p>'+ ' · '.join(f'<a href="{j+1}.html">{j+1}. {escape(markers[j])}</a>' for j in range(4))+'</p></html>'
    (directory/f'{i+1}.html').write_text(page,encoding='utf-8')
print('Demo pages ready')
