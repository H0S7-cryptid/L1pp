"""Rebuild the report and six plots from real CSV measurements (ReportLab)."""
from pathlib import Path
from html import escape
from statistics import quantiles
import csv, json, math, subprocess, textwrap, os, re
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Table,
    TableStyle, PageBreak, Image, Preformatted, KeepTogether)
from reportlab.graphics.shapes import Drawing, Rect, Line, String, Circle, PolyLine, Polygon
from reportlab.graphics import renderPDF, renderSVG

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output'; QA=OUT/'qa'; CHARTS=OUT/'charts'
for directory in (OUT,QA,CHARTS): directory.mkdir(exist_ok=True,parents=True)
for name,file in [('Arial','arial.ttf'),('Arial-Bold','arialbd.ttf'),('Mono','consola.ttf')]:
    pdfmetrics.registerFont(TTFont(name,str(Path(os.environ.get('WINDIR','C:/Windows'))/'Fonts'/file)))
pdfmetrics.registerFontFamily('Arial',normal='Arial',bold='Arial-Bold',italic='Arial',boldItalic='Arial-Bold')
NAVY=colors.HexColor('#16324F'); BLUE=colors.HexColor('#2374AB'); ORANGE=colors.HexColor('#DB6E25')
GRAY=colors.HexColor('#64748B'); PALE=colors.HexColor('#F1F5F9'); GRID=colors.HexColor('#D9D9D9')
def read(name):
    with (ROOT/'results'/name).open(encoding='utf-8-sig') as stream: return list(csv.DictReader(stream))
summary=read('summary.csv'); raw=read('raw.csv'); main=read('main.csv'); accuracy=read('accuracy.csv')
meta=dict(line.split('=',1) for line in (ROOT/'results/metadata.txt').read_text().splitlines())
verify=json.loads((ROOT/'results/verification.json').read_text(encoding='utf-8'))
hardware=json.loads((ROOT/'results/hardware.json').read_text(encoding='utf-8-sig'))
reference=float(verify['reference_decimal_60_digits']); N=int(meta['N']); pmax=int(meta['max_threads'])
techs=['POSIX','std::thread']
groups=[[r for r in summary if r['technology']==tech] for tech in techs]
best=[min(rows,key=lambda r:float(r['seconds'])) for rows in groups]
POPPLER=Path(os.environ.get('CODEX_PDFTOPPM',str(Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/native/poppler/Library/bin/pdftoppm.exe')))
def rasterize(pdf,target,dpi=145):
    subprocess.run([str(POPPLER),'-singlefile','-r',str(dpi),'-png',str(pdf),str(target)],check=True,capture_output=True)
def label(d,x,y,text,size=9,color=GRAY,anchor='start',font='Arial'):
    d.add(String(x,y,text,fontName=font,fontSize=size,fillColor=color,textAnchor=anchor))

def plot(filename,title,ytitle,series,limits=None,format_y=None,iqr=False,note=''):
    d=Drawing(740,370)
    d.add(Rect(0,0,740,370,fillColor=colors.white,strokeColor=None))
    label(d,24,341,title,18,NAVY,font='Arial-Bold')
    label(d,78,312,ytitle,12,GRAY)
    left,bottom,width,height=78,65,625,226
    all_y=[y for _,_,values in series for y in values]
    lo,hi=limits if limits else (min(0,min(all_y)),max(all_y)*1.13)
    if hi==lo: hi=lo+1
    X=lambda x:left+(x-1)/(pmax-1)*width
    Y=lambda y:bottom+(y-lo)/(hi-lo)*height
    for tick in range(6):
        value=lo+(hi-lo)*tick/5
        y=Y(value)
        d.add(Line(left,y,left+width,y,strokeColor=GRID,strokeWidth=.6))
        label(d,left-10,y-3,format_y(value) if format_y else f'{value:.2f}',12,anchor='end')
    for p in range(1,pmax+1):
        x=X(p)
        d.add(Line(x,bottom-4,x,bottom,strokeColor=GRAY,strokeWidth=.7))
        label(d,x,bottom-17,str(p),12,anchor='middle')
    d.add(Line(left,bottom,left+width,bottom,strokeColor=GRAY))
    d.add(Line(left,bottom,left,bottom+height,strokeColor=GRAY))
    for idx,(name,color,values) in enumerate(series):
        points=[v for p,y in enumerate(values,1) for v in (X(p),Y(y))]
        d.add(PolyLine(points,strokeColor=color,strokeWidth=1.8,strokeDashArray=[5,3] if name.startswith('Идеал') else None))
        for p,y in enumerate(values,1):
            if iqr and name in techs:
                runs=[float(r['seconds']) for r in raw if r['technology']==name and int(r['threads'])==p]
                q1,_,q3=quantiles(runs,n=4,method='inclusive')
                d.add(Line(X(p),Y(max(lo,q1)),X(p),Y(min(hi,q3)),strokeColor=color,strokeWidth=1))
            d.add(Circle(X(p),Y(y),2.5,fillColor=color,strokeColor=colors.white,strokeWidth=.5))
        legend_x=80+idx*205
        d.add(Line(legend_x,24,legend_x+20,24,strokeColor=color,strokeWidth=2))
        label(d,legend_x+27,21,name,11,GRAY)
    label(d,703,35,'Количество потоков p',11,anchor='end')
    if note: label(d,24,4,note,10,GRAY)
    renderPDF.drawToFile(d,str(QA/f'{filename}.pdf'))
    renderSVG.drawToFile(d,str(CHARTS/f'{filename}.svg'))
    rasterize(QA/f'{filename}.pdf',CHARTS/filename)
    return d

time_series=[(t,c,[float(r['seconds']) for r in g]) for t,c,g in zip(techs,[BLUE,ORANGE],groups)]
speed_series=[(t,c,[float(r['speedup']) for r in g]) for t,c,g in zip(techs,[BLUE,ORANGE],groups)]
eff_series=[(t,c,[float(r['efficiency_percent']) for r in g]) for t,c,g in zip(techs,[BLUE,ORANGE],groups)]
plot('01-time','Время выполнения','Время, с',time_series,iqr=True,note='Точки — медианы 7 повторов; вертикальные отрезки — межквартильный размах.')
plot('02-speedup','Ускорение относительно одного потока','S(p) = T(1) / T(p)',speed_series)
plot('03-efficiency','Эффективность использования потоков','E(p), %',eff_series+[('Идеал 100 %',GRAY,[100]*pmax)])
plot('04-result','Результаты вычисления интеграла','I(p) = I_ref + y × 10^-8',
     [(t,c,[(float(r['result'])-reference)*1e8 for r in g]) for t,c,g in zip(techs,[BLUE,ORANGE],groups)]+
     [('Эталон I_ref',GRAY,[0]*pmax)],limits=(-2.1,.3),format_y=lambda x:f'{x:.2f}',
     note=f'I_ref = {reference:.16f}; кривые POSIX и std::thread практически совпадают.')
plot('05-ideal-speedup','Идеальное и фактическое ускорение','S(p)',speed_series+[('Идеал S(p) = p',GRAY,list(range(1,pmax+1)))])
plot('06-relative-efficiency','Эффективность относительно идеального ускорения','S(p) / S_ideal(p), доля',
     [(t,c,[float(r['speedup'])/int(r['threads']) for r in g]) for t,c,g in zip(techs,[BLUE,ORANGE],groups)]+
     [('Идеальная доля 1',GRAY,[1]*pmax)],note='Это та же эффективность, что на графике 3, выраженная долей вместо процентов.')

# An activity diagram with explicit fork/join and the measurement loop.
d=Drawing(520,670)
def box(x,y,w,h,lines):
    d.add(Rect(x,y,w,h,rx=6,ry=6,fillColor=PALE,strokeColor=NAVY,strokeWidth=.8))
    for i,s in enumerate(lines): label(d,x+w/2,y+h/2+(len(lines)-1)*6-i*12-3,s,9,NAVY,'middle')
def arrow(x1,y1,x2,y2):
    d.add(Line(x1,y1,x2,y2,strokeColor=NAVY,strokeWidth=1))
    angle=math.atan2(y2-y1,x2-x1)
    points=[x2,y2,x2-7*math.cos(angle-.4),y2-7*math.sin(angle-.4),x2-7*math.cos(angle+.4),y2-7*math.sin(angle+.4)]
    d.add(Polygon(points,fillColor=NAVY,strokeColor=None))
d.add(Circle(260,654,6,fillColor=NAVY,strokeColor=None)); arrow(260,647,260,633)
box(115,596,290,37,['Ввод N, p_main, p_max','Проверка диапазонов и доступности файлов'])
arrow(260,596,260,583)
d.add(Polygon([260,583,337,561,260,539,183,561],fillColor=colors.white,strokeColor=NAVY))
label(d,260,558,'Ввод корректен?',9,NAVY,'middle')
arrow(337,561,385,561); label(d,350,568,'нет',8); box(385,543,120,36,['Сообщение об ошибке','Код возврата 1'])
arrow(445,543,445,531); d.add(Circle(445,522,8,fillColor=colors.white,strokeColor=NAVY)); d.add(Circle(445,522,5,fillColor=NAVY))
arrow(260,539,260,522); label(d,270,529,'да',8)
box(105,480,310,42,['h = 3/N; прогрев; последовательная база','Выбор API, p и повтора по расписанию'])
arrow(260,480,260,461)
box(110,420,300,41,['Разбиение N индексов на p блоков','Создание p потоков выбранного API'])
arrow(260,420,260,405)
d.add(Rect(80,400,360,5,fillColor=NAVY,strokeColor=None))
arrow(150,400,150,382); arrow(370,400,370,382)
box(45,324,210,58,['Поток 0: локальная сумма Кэхэна','x_i = 2 + (i+1)h','Завершение рабочего потока'])
box(280,324,210,58,['Поток p-1: локальная сумма','Тот же шаг и свои индексы','Завершение рабочего потока'])
label(d,267,350,'…',14,NAVY,'middle'); arrow(150,324,150,308); arrow(370,324,370,308)
d.add(Rect(80,303,360,5,fillColor=NAVY,strokeColor=None)); arrow(260,303,260,287)
box(100,240,320,47,['Ожидание ВСЕХ потоков','POSIX: pthread_join(handle, &returned_value)','std::thread: join(), затем чтение result[t]'])
arrow(260,240,260,223)
box(105,185,310,38,['Редукция сумм; I_N = h × сумма','Остановка таймера; сохранение замера'])
arrow(260,185,260,167)
d.add(Polygon([260,167,344,145,260,123,176,145],fillColor=colors.white,strokeColor=NAVY))
label(d,260,142,'Есть конфигурации?',9,NAVY,'middle')
label(d,355,152,'да',8); d.add(PolyLine([344,145,505,145,505,500,415,500],strokeColor=NAVY,strokeWidth=1))
arrow(438,500,415,500)
arrow(260,123,260,105); label(d,270,112,'нет',8)
box(85,59,350,46,['Медианы, ускорение, эффективность, погрешность','Отдельные таблицы POSIX, std::thread и основного расчёта','CSV → шесть графиков → отчёт'])
arrow(260,59,260,39); d.add(Circle(260,29,8,fillColor=colors.white,strokeColor=NAVY)); d.add(Circle(260,29,5,fillColor=NAVY))
renderSVG.drawToFile(d,str(OUT/'activity.svg')); renderPDF.drawToFile(d,str(QA/'activity.pdf'))
rasterize(QA/'activity.pdf',OUT/'activity',160)
(OUT/'activity.puml').write_text('''@startuml
start
:Ввод N, p_main, p_max;
if (Ввод корректен?) then (да)
  :Шаг h=3/N, прогрев, последовательная база;
  repeat
    :Выбор API, p, повтора;
    :Разбиение индексов с остатком;
    :Создание p потоков;
    fork
      :Локальная сумма в потоке 0;
    fork again
      :Локальные суммы в остальных потоках;
    end fork
    if (API POSIX?) then (да)
      :pthread_join с получением возвращённого double*;
      :Освобождение возвращённой памяти;
    else (std::thread)
      :join всех потоков, чтение отдельных результатов;
    endif
    :Редукция, остановка таймера, запись замера;
  repeat while (Есть конфигурации?) is (да)
  :Таблицы, точность, CSV, графики и отчёт;
else (нет)
  :Ошибка ввода, код 1;
endif
stop
@enduml
''',encoding='utf-8')

styles={
 'body':ParagraphStyle('body',fontName='Arial',fontSize=10.5,leading=15,spaceAfter=9),
 'h1':ParagraphStyle('h1',fontName='Arial-Bold',fontSize=17,leading=22,spaceAfter=15,keepWithNext=True),
 'h2':ParagraphStyle('h2',fontName='Arial-Bold',fontSize=12,leading=16,spaceBefore=10,spaceAfter=8,keepWithNext=True),
 'caption':ParagraphStyle('caption',fontName='Arial',fontSize=9,leading=12,spaceBefore=5,spaceAfter=12,textColor=GRAY),
 'cell':ParagraphStyle('cell',fontName='Arial',fontSize=8.5,leading=11),
 'head':ParagraphStyle('head',fontName='Arial-Bold',fontSize=8.5,leading=11,textColor=colors.white),
 'code':ParagraphStyle('code',fontName='Mono',fontSize=7.2,leading=9.2,spaceAfter=6),
 'title':ParagraphStyle('title',fontName='Arial-Bold',fontSize=25,leading=32,spaceAfter=20),
}
story=[]; html=[]
def p(text,style='body'):
    story.append(Paragraph(math_markup(text),styles[style])); html.append(f'<p class="{style}">{text}</p>')
def math_markup(text):
    text=text.replace('I₂N','I<sub>2N</sub>').replace('I_N','I<sub>N</sub>')
    supers=str.maketrans('⁰¹²³⁴⁵⁶⁷⁸⁹⁻⁺⁽⁾ᵏ','0123456789−+()k')
    subs=str.maketrans('₀₁₂₃₄₅₆₇₈₉ₖₜ','0123456789kt')
    text=re.sub('[⁰¹²³⁴⁵⁶⁷⁸⁹⁻⁺⁽⁾ᵏ]+',lambda m:'<super>'+m[0].translate(supers)+'</super>',text)
    return re.sub('[₀₁₂₃₄₅₆₇₈₉ₖₜ]+',lambda m:'<sub>'+m[0].translate(subs)+'</sub>',text)
def heading(text): p(text,'h1')
def sub(text): p(text,'h2')
def page(): story.append(PageBreak()); html.append('<div class="pagebreak"></div>')
def table(headers,rows,widths=None):
    items=[[Paragraph(math_markup(escape(str(v))),styles['head']) for v in headers]]
    items += [[Paragraph(math_markup(escape(str(v)).replace('\n','<br/>')),styles['cell']) for v in row] for row in rows]
    t=Table(items,colWidths=widths,repeatRows=1,hAlign='LEFT')
    t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),NAVY),('GRID',(0,0),(-1,-1),.45,GRID),
       ('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,PALE]),('VALIGN',(0,0),(-1,-1),'MIDDLE'),
       ('LEFTPADDING',(0,0),(-1,-1),7),('RIGHTPADDING',(0,0),(-1,-1),7),
       ('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),6)]))
    story.extend([t,Spacer(1,10)])
    html.append('<table><thead><tr>'+''.join(f'<th>{escape(str(x))}</th>' for x in headers)+'</tr></thead><tbody>'+''.join('<tr>'+''.join(f'<td>{escape(str(x))}</td>' for x in row)+'</tr>' for row in rows)+'</tbody></table>')
def figure(path,width=490,caption='',maxheight=None):
    from PIL import Image as PILImage
    with PILImage.open(path) as image: w,h=image.size
    height=width*h/w
    if maxheight and height>maxheight: width*=maxheight/height; height=maxheight
    img=Image(str(path),width=width,height=height,hAlign='CENTER')
    block=[img]
    if caption: block.append(Paragraph(caption,styles['caption']))
    story.append(KeepTogether(block))
    html.append(f'<figure><img src="{path.relative_to(OUT).as_posix()}"><figcaption>{caption}</figcaption></figure>')
def code(text):
    story.append(Preformatted(text,styles['code'])); html.append('<pre>'+escape(text)+'</pre>')
def fnum(value,d=9): return f'{float(value):.{d}f}'

# Cover page.
p('Лабораторная работа 1','title')
p('Параллельное численное интегрирование<br/>методом правых прямоугольников','h1')
p('Две реализации на C++: POSIX Threads и std::thread')
story.append(Spacer(1,28))
table(['Параметр','Значение'],[
 ['Выполнили','Виткулевский Т. В.\nВолков К. В.'],['Группа','АВТ-412'],
 ['Преподаватели','Ландовский В. В., доцент, к. т. н. кафедры АСУ;\nДворецкая В. К., ассистент кафедры АСУ'],
 ['Вариант','Правые прямоугольники, f(x) = (1+x²)/(x³√(2+x)), [2;5]'],
 ['Дата эксперимента','20 октября 2026 года'],
 ['Основной запуск',f'N = {N:,}; p = {meta["main_threads"]}; p_max = {pmax}; {meta["repeats"]} повторов'],
], [118,372])
p('Цель работы — освоить численное интегрирование, обоснованный выбор шага и оценку погрешности, реализовать параллельное вычисление двумя средствами C++ и исследовать масштабируемость.')
p(f'Получено I_N = {float(main[0]["result"]):.15f}; аналитический эталон I = {reference:.16f}. Абсолютная ошибка составляет {abs(reference-float(main[0]["result"])):.5e}. Оба API дают одинаковый результат в пределах округления.')

page(); heading('1 Постановка задачи и численный метод')
p('Вычисляется определённый интеграл I = ∫₂⁵ (1+x²)/(x³√(2+x)) dx. Функция непрерывна и положительна на всём отрезке, поэтому интеграл совпадает с площадью под её графиком.')
sub('Формула правых прямоугольников')
p('Отрезок разбивается на N равных частей: h = (b−a)/N = 3/N. На каждом подотрезке высота прямоугольника берётся в правом конце: x<sub>i</sub> = a+ih, i = 1,…,N.')
code('I_N = h * sum(f(a + i*h), i=1..N)\nВ коде: i=0..N-1, узел x = a + (i+1)*h')
p('Функция убывает, поэтому правые прямоугольники занижают интеграл. Параллелизация меняет порядок суммирования, но не сетку узлов и не формулу метода.')
sub('Сопоставление погрешностей методов задания')
table(['Метод','Порядок','Граница |I−I_N|','Ограничение N'],[
 ['Левые / правые прямоугольники','O(h)','L h M₁ / 2','N ≥ 1'],
 ['Средние прямоугольники','O(h²)','L h² M₂ / 24','N ≥ 1'],
 ['Трапеции','O(h²)','L h² M₂ / 12','N ≥ 1'],
 ['Симпсон','O(h⁴)','L h⁴ M₄ / 180','N чётное'],
 ['Ньютон–Котес 3/8','O(h⁴)','L h⁴ M₄ / 80','N кратно 3'],
], [147,55,174,114])
p('Здесь L = b−a, Mₖ = max |f⁽ᵏ⁾(x)| на [a;b]; для соответствующей границы нужна непрерывная производная указанного порядка. В строке Ньютона–Котеса рассматривается составная формула 3/8 с интерполяционным полиномом третьей степени. В программе по варианту реализованы только правые прямоугольники.')

page(); heading('2 Шаг интегрирования и оценка погрешности')
p('Представим f(x) = (1/x + 1/x³) / √(x+2). Тогда производная имеет вид:')
code("f'(x) = -(x^-2 + 3*x^-4)/sqrt(x+2)\n        -(x^-1 + x^-3)/(2*(x+2)^(3/2))")
p('Каждое слагаемое в |f′(x)| положительно и убывает при x &gt; 0. Максимум на [2;5] достигается при x = 2: M₁ = 33/128 = 0,2578125.')
code('|I - I_N| <= (b-a)*h*M1/2 = 297/(256*N) = 1.16015625/N\nN >= ceil(1.16015625/eps),  h = 3/N')
table(['Требуемая ε','Достаточное N','Шаг h','Граница ошибки'],[
 [f'{eps:.0e}',str(math.ceil(1.16015625/eps)),f'{3/math.ceil(1.16015625/eps):.6e}',f'{1.16015625/math.ceil(1.16015625/eps):.6e}']
 for eps in [1e-3,1e-5,1e-7]], [110,115,130,135])
p(f'Для эксперимента N = {N:,}, h = {3/N:.6e}, теоретическая граница равна {1.16015625/N:.8e}. Это оценка ошибки квадратуры в точной арифметике; при очень малом шаге дополнительно действует округление double.')
sub('Аналитический эталон и независимая проверка')
p('Подстановка t = √(x+2) сводит интеграл к рациональной функции 2/(t²−2) + 2/(t²−2)³. Первообразная:')
code('F(x) = 35/(32*sqrt(2))*ln((sqrt(x+2)-sqrt(2))/(sqrt(x+2)+sqrt(2)))\n     + sqrt(x+2)*(3/(16*x) - 1/(4*x*x))\nI = F(5) - F(2)')
p('Эталон, вычисленный Decimal с точностью 60 значащих цифр:')
code(verify['reference_decimal_60_digits'])
p(f'Независимая составная формула Симпсона на 16 384 частях в арифметике Decimal отличается от аналитического эталона на {float(verify["reference_check_difference"]):.3e}. Это контроль эталона, а не замена метода варианта.')
sub('Оценка Рунге')
p('Порядок метода q = 1. Для уточнённого результата I₂N оценка Рунге равна |I₂N−I_N|/(2¹−1) = |I₂N−I_N|; для грубого I_N асимптотическая оценка вдвое больше. Наблюдаемый порядок вычисляется как log₂(|I−I_N|/|I−I₂N|).')

page(); heading('3 Диаграмма деятельности')
figure(OUT/'activity.png',width=455,maxheight=620,caption='Рисунок 1. Алгоритм одного эксперимента. Вилкой и объединением показаны рабочие потоки выбранного API; технологии измеряются по очереди. Ошибки создания обрабатываются с ожиданием уже запущенных потоков.')

page(); heading('4 Организация программы')
sub('Распределение вычислений')
p('Для p потоков q = N div p, r = N mod p. Поток t получает начало lₜ = tq + min(t,r) и конец uₜ = lₜ + q + [t &lt; r]; индексы обрабатываются на полуинтервале [lₜ,uₜ). Соседние блоки стыкуются без пропусков и перекрытий, а их размеры различаются не более чем на единицу.')
sub('POSIX Threads')
p('pthread_create передаёт потоку структуру с границами блока и шагом. Рабочая функция выделяет double в куче, записывает локальную сумму и возвращает указатель; основной поток получает именно возвращённое значение вызовом pthread_join(handle, &amp;raw). После чтения unique_ptr освобождает память; указатель на локальную переменную стека не возвращается.')
sub('std thread')
p('Функция worker(first, last, a, h, double&amp; result) получает параметры отдельно. В конструктор std::thread передаются first, last, A, h и std::ref(results[t]); структура параметров не используется. Каждый поток пишет только в свой элемент заранее выделенного массива, а чтение происходит после join, поэтому гонки данных нет.')
sub('Точность и обработка ошибок')
p('Локальные суммы и окончательная редукция используют суммирование Кэхэна. При частичном сбое создания потоков уже запущенные потоки завершаются до освобождения их аргументов; неуспешный pthread_join приводит к диагностике и остановке вместо обращения к недействительным данным.')
table(['Файл','Назначение'],[
 ['integration.hpp / integration.cpp','Интерфейс, функция варианта, сетка, последовательная сумма, эталон'],
 ['posix.cpp','POSIX-реализация и получение результата через pthread_join'],
 ['std_thread.cpp','Реализация std::thread с отдельными аргументами'],
 ['main.cpp','Ввод, проверка, измерения, таблицы, экспорт CSV, самопроверка'],
], [170,320])
p('Использована предоставленная Pthreads-win32 2.9.1, pthreadVC2.lib и pthreadVC2.dll x64. Для современного MSVC определено HAVE_STRUCT_TIMESPEC после подключения time.h, чтобы старый pthread.h не переопределял timespec из UCRT.')

page(); heading('5 Методика эксперимента и проверка')
table(['Параметр','Значение'],[
 ['Процессор',hardware['ProcessorNameString']],['ОС и число логических потоков','Windows x64; '+meta['hardware_threads']],
 ['Компилятор и сборка','MSVC 14.51.36231, Visual Studio 18; Release x64, оптимизация /O2'],
 ['Число разбиений',f'{N:,}'],['Основное / максимальное число потоков',meta['main_threads']+' / '+meta['max_threads']],
 ['Повторы и прогрев',meta['repeats']+' повторов; один прогрев каждой конфигурации'],
 ['Таймер и статистика','std::chrono::steady_clock; медиана'],
 ['Порядок измерений','API и p перемешиваются между повторами, seed 412'],
], [185,305])
p('Измерение охватывает выделение рабочих массивов, создание потоков, вычисление, join, редукцию и освобождение временных ресурсов. Консольный вывод, запись CSV, прогрев и вычисление погрешности вынесены за измеряемый участок. Однопоточная база каждого API также создаёт один рабочий поток; прямой последовательный цикл измерен отдельно.')
code('S_API(p) = T_API(1) / T_API(p)\nE_API(p) = S_API(p) / p * 100%\nS_ideal(p) = p\nS_direct(p) = T_direct / T_API(p)')
p('Короткие измерения зависят от планировщика, фоновой нагрузки и частоты процессора. Небольшое превышение 100 % на отдельных конфигурациях отражает отличие условий исполнения от однопоточной базы и не доказывает устойчивое сверхлинейное масштабирование.')
sub('Проверки корректности')
p('Встроенный --self-test проверяет N = 1, 2, 7, 31, 1000, 10000 и допустимые p до 8: покрытие всех индексов, совпадение обеих технологий с последовательной суммой, границу ошибки и первый порядок сходимости. Отдельный Python-скрипт подтвердил медианы, ускорения и эффективность всех 32 строк таблицы по 231 исходному замеру, а также отклонение девяти некорректных вариантов командной строки.')
p(f'Максимальный размах значений интеграла между всеми записанными запусками: {verify["max_parallel_result_spread"]:.3e}. Это на много порядков меньше ошибки дискретизации; итог всех проверок — PASS.')

page(); heading('6 Основной результат и исследование точности')
table(['Режим','p','Время, с','Интеграл','Tseq / Tp'],[
 [r['technology'],r['threads'],fnum(r['seconds']),fnum(r['result'],15),fnum(r['speedup_vs_direct'],4)] for r in main
], [95,30,85,190,90])
p('В этой таблице основание ускорения — прямой последовательный цикл. В сводных таблицах следующего раздела основание — один рабочий поток соответствующей технологии; поэтому значения ускорений отличаются.')
sub('Абсолютная погрешность и сходимость')
table(['N','h','I_N','|I−I_N|','Порядок'],[
 [r['n'],f'{float(r["h"]):.2e}',fnum(r['result'],12),f'{float(r["absolute_error"]):.4e}',fnum(r['observed_order'],5)] for r in accuracy
], [72,72,160,112,74])
sub('Оценка Рунге и теоретическая граница')
table(['N','Рунге для I₂N','Граница для I_N','Относительная ошибка I_N'],[
 [r['n'],f'{float(r["runge_estimate"]):.5e}',f'{float(r["bound"]):.5e}',f'{float(r["relative_error"]):.5e}'] for r in accuracy
], [80,125,125,160])
p('С ростом N ошибка убывает, а наблюдаемый порядок приближается к единице. Отрицательный знак I_N−I согласуется с убыванием функции; заданная теоретическая граница выполнена для каждой проверенной сетки.')

page(); heading('7 Сводные таблицы результатов')
for tech,rows in zip(techs,groups):
    sub(tech)
    table(['Потоки','Время, с','Результат','Ускорение','Эффект., %'],[
      [r['threads'],fnum(r['seconds']),fnum(r['result'],15),fnum(r['speedup'],4),fnum(r['efficiency_percent'],2)] for r in rows
    ], [50,85,185,85,85])
    p('Ускорение рассчитано относительно одного рабочего потока '+tech+'. Данные — медианы семи повторов; все исходные времена сохранены в results/raw.csv.','caption')
    if tech==techs[0]: page(); heading('7 Сводные таблицы результатов продолжение')

page(); heading('8 Разброс измерений и идеальная эффективность')
table(['p','POSIX Q1–Q3, мс','std Q1–Q3, мс','POSIX S/p','std S/p'],[
 [str(p),*[' – '.join(f'{v*1000:.3f}' for v in quantiles([float(r['seconds']) for r in raw if r['technology']==tech and int(r['threads'])==p],n=4,method='inclusive')[::2]) for tech in techs],
  *[f'{float(groups[k][p-1]["speedup"])/p:.4f}' for k in range(2)]] for p in range(1,pmax+1)
], [30,135,135,95,95])
p('Q1–Q3 показывает центральную половину замеров и помогает оценить устойчивость сравнения. Доля S/p равна эффективности, делённой на 100; идеал равен 1. Оптимум по минимальной медиане относится к данному запуску, а не является универсальной настройкой компьютера.')
table(['Технология','Минимум T(p), с','p при минимуме','S(p)','E(p), %'],[
 [r['technology'],fnum(r['seconds']),r['threads'],fnum(r['speedup'],4),fnum(r['efficiency_percent'],2)] for r in best
], [100,115,95,90,90])

descriptions=[
 ('01-time','9.1 Время выполнения',f'Время резко уменьшается при переходе от одного к нескольким потокам. Минимумы медиан: POSIX — {float(best[0]["seconds"]):.6f} с при p = {best[0]["threads"]}; std::thread — {float(best[1]["seconds"]):.6f} с при p = {best[1]["threads"]}. Неровности после p = 4 согласуются с влиянием распределения нагрузки и фоновых процессов; по этим данным нельзя выделить единственную причину.'),
 ('02-speedup','9.2 Ускорение',f'Максимальное измеренное ускорение POSIX равно {float(best[0]["speedup"]):.3f}, std::thread — {float(best[1]["speedup"]):.3f}. Ускорение вычислено по собственной однопоточной базе каждого API, поэтому сравнивать технологии по абсолютному времени корректнее, чем только по S(p).'),
 ('03-efficiency','9.3 Эффективность использования потоков','Эффективность близка к 100 % на малом числе потоков и заметно падает при дальнейшем увеличении p. Это означает, что добавление потоков даёт всё меньшую отдачу на один поток; отдельные значения немного выше 100 % объясняются изменчивостью измерений и условий исполнения.'),
 ('04-result','9.4 Результаты вычислений','Для всех p используется одна и та же сетка. На графике показано I_N = I_ref + y·10⁻⁸, чтобы наглядно видеть общую ошибку относительно эталона; две технологические кривые практически полностью совпадают. Максимальное различие между записанными результатами равно 5,55·10⁻¹⁷.'),
 ('05-ideal-speedup','9.5 Идеальное и фактическое ускорение','Идеальное ускорение S = p растёт линейно, а фактическое выходит на область насыщения около 4–5. Создание потоков, объединение результатов и ограничения вычислительных ресурсов не масштабируются идеально; при p выше 12 логических потоков возникает также переподписка ресурсов.'),
 ('06-relative-efficiency','9.6 Эффективность относительно идеального ускорения','Доля S/S_ideal = S/p показывает, какую часть идеального ускорения даёт конфигурация. Этот показатель математически совпадает с эффективностью из графика 3, выраженной долей вместо процентов; оба графика приведены отдельно согласно заданию.'),
]
for i,(name,title,description) in enumerate(descriptions):
    if i%2==0: page(); heading('9 Графики эксперимента' if i==0 else '9 Графики эксперимента продолжение')
    sub(title)
    figure(CHARTS/f'{name}.png',width=445,caption=f'Рисунок {i+2}. {title[4:]}.')
    p(description)

page(); heading('10 Демонстрация программы')
p('Программа запускается без аргументов для интерактивного ввода N, числа потоков основного вычисления и максимального числа потоков. Для точного повторения эксперимента использован эквивалентный запуск с аргументами:')
code('.\\x64\\Release\\L1pp.exe 20000000 8 16 7 results')
p('Ниже приведены реальные скриншоты браузерного просмотра stdout из results/console.txt. Это представление фактического вывода программы; окно терминала на изображениях не имитируется.')
figure(OUT/'demo/01-posix.png',width=430,maxheight=495,caption='Рисунок 8. Таблица POSIX из протокола запуска.')
page(); heading('10 Демонстрация программы продолжение')
figure(OUT/'demo/02-std-thread.png',width=420,maxheight=490,caption='Рисунок 9. Таблица std::thread из того же запуска.')
p('Поля Time, s, Integral, Speedup и Eff., % соответствуют времени, интегралу, ускорению и эффективности. Отдельная таблица следующей страницы сравнивает p = 8 с прямым однопоточным вычислением.')
page(); heading('10 Основное вычисление и точность на экране')
# Crop only empty lower background, preserving the complete visible output and its context.
from PIL import Image as PILImage
for name,height in [('03-main',355),('04-accuracy',520)]:
    source=OUT/f'demo/{name}.png'
    with PILImage.open(source) as image:
        image.crop((0,0,image.width,min(height,image.height))).save(OUT/f'demo/{name}-crop.png')
    figure(OUT/f'demo/{name}-crop.png',width=490,caption='Рисунок 10. Основное вычисление.' if name=='03-main' else 'Рисунок 11. Сходимость, эталон, граница ошибки и шаг.')

page(); heading('11 Выводы')
sub('Эффективность параллелизации')
p(f'Разделение суммы даёт ускорение до {float(best[0]["speedup"]):.2f} для POSIX и {float(best[1]["speedup"]):.2f} для std::thread относительно одного рабочего потока. После нескольких потоков рост становится неравномерным, а эффективность падает, поэтому увеличение p само по себе не гарантирует меньшего времени.')
sub('Сравнение технологий')
p(f'При одинаковых восьми потоках POSIX занял {float(main[1]["seconds"]):.7f} с, std::thread — {float(main[2]["seconds"]):.7f} с; различие медиан составляет {abs(float(main[1]["seconds"])-float(main[2]["seconds"]))/float(main[2]["seconds"])*100:.2f} %. На фоне разброса это не позволяет объявить один API устойчиво быстрее; std::thread удобнее для типизированной передачи аргументов, а POSIX явно возвращает указатель через join.')
sub('Закон Амдала')
p('Модель S(p) = 1 / (s + (1−s)/p) ограничивает ускорение при наличии последовательной доли s. Измеренное насыщение согласуется с последовательными затратами и накладными расходами, но неоднородность ресурсов, частота и планировщик нарушают предпосылки простейшей модели, поэтому по одной кривой нельзя надёжно оценить постоянную долю s.')
sub('Точность вычислений')
p(f'Ошибка правых прямоугольников при N = {N:,} равна примерно {abs(reference-float(main[0]["result"])):.3e}, что меньше границы {1.16015625/N:.3e}; удвоение N асимптотически уменьшает ошибку вдвое. Изменение числа потоков даёт различия не более 5,55·10⁻¹⁷, поэтому точность здесь определяется шагом интегрирования, а не API.')
sub('Оптимальная конфигурация')
p(f'По минимальной медиане данного эксперимента оптимальны {best[0]["threads"]} потоков POSIX и {best[1]["threads"]} потоков std::thread. Это локальный результат для N = {N:,} и текущей нагрузки: для устойчивой рекомендации следует повторить измерения в сопоставимых условиях и сопоставить межквартильные интервалы.')
sub('Практическая значимость')
p('Задачи с независимыми слагаемыми удобно распараллеливать через локальные суммы и последующее объединение без блокировок на каждом узле. Полученный подход применим к квадратурам и другим вычислительным суммам, а выбор N по оценке ошибки позволяет не тратить время на избыточную точность.')

page(); heading('12 Воспроизведение и источники')
sub('Сборка и запуск')
code('PowerShell, из корня проекта:\n.\\build.ps1\n.\\x64\\Release\\L1pp.exe\n\nВоспроизведение замеров:\n.\\run-experiments.ps1\n\nПроверки:\n.\\x64\\Release\\L1pp.exe --self-test\npython scripts/verify.py\npython scripts/make_report.py')
p('Сборка в Visual Studio: открыть L1pp.slnx, выбрать Release и x64. В проекте настроены include, import library и копирование pthreadVC2.dll рядом с исполняемым файлом. Для генератора отчёта нужны Python, ReportLab, Pillow и Poppler; пути к Poppler можно задать переменной CODEX_PDFTOPPM.')
sub('Исходники и GitHub')
p('Полный текст C++ приведён в приложении и находится в папке L1pp. Репозиторий GitHub отсутствует: по прямому указанию заказчика создавать его не требуется, поэтому внешняя ссылка не приводится. Все файлы для последующего размещения включены в комплект работы.')
sub('Использованные материалы')
p('1. Условие лабораторной работы и предоставленный скриншот варианта.')
p('2. Pthreads-win32 2.9.1: pthread/Pre-built.2/README, include/pthread.h; комплект библиотек x64 из предоставленной папки pthread.')
p('3. Рабочий проект стандарта C++, конструктор std::thread: <link href="https://eel.is/c++draft/thread.thread.constr" color="#2374AB">eel.is/c++draft/thread.thread.constr</link>. Конструктор принимает вызываемый объект и отдельные аргументы; обычное возвращаемое значение функции потока игнорируется.')
p('4. Собственные экспериментальные данные: results/raw.csv, summary.csv, main.csv, accuracy.csv; проверка — results/verification.json.')
p('Материалы POSIX Threads for Win.pdf и 2026_ПП_Л1.pdf в предоставленной папке отсутствуют. Они не включены в список использованных источников; свойства библиотеки проверены по её локальным заголовкам и README.')

for filename in ['integration.hpp','integration.cpp','posix.cpp','std_thread.cpp','main.cpp']:
    page(); heading('Приложение Текст программы'); sub(filename)
    text=(ROOT/'L1pp'/filename).read_text(encoding='utf-8-sig')
    lines=[]
    for lineno,line in enumerate(text.splitlines(),1):
        chunks=textwrap.wrap(line,width=104,replace_whitespace=False,drop_whitespace=False) or ['']
        lines.append(f'{lineno:3}  '+chunks[0])
        lines.extend('     '+part for part in chunks[1:])
    code('\n'.join(lines))

def footer(canvas,doc):
    canvas.saveState(); canvas.setFont('Arial',8); canvas.setFillColor(GRAY)
    canvas.drawString(52,27,'АВТ-412 · Численное интегрирование · POSIX и std::thread')
    canvas.drawRightString(A4[0]-52,27,str(doc.page)); canvas.restoreState()
doc=SimpleDocTemplate(str(OUT/'report.pdf'),pagesize=A4,rightMargin=52,leftMargin=52,
    topMargin=42,bottomMargin=43,title='Лабораторная работа 1 — параллельное численное интегрирование',
    author='Виткулевский Т. В.; Волков К. В.')
doc.build(story,onFirstPage=footer,onLaterPages=footer)
css='''body{font:16px/1.55 Arial,sans-serif;max-width:1000px;margin:40px auto;padding:0 24px;color:#172333;background:#fff}
.title{font-size:34px;font-weight:bold}.h1{font-size:26px;font-weight:bold;margin-top:35px}.h2{font-size:20px;font-weight:bold;margin-top:24px}
table{width:100%;border-collapse:collapse;margin:18px 0;font-size:14px}th{background:#16324f;color:white}td,th{padding:10px;border:1px solid #d9d9d9;text-align:left}tr:nth-child(even){background:#f1f5f9}
figure{margin:20px 0}img{max-width:100%;max-height:800px;object-fit:contain}figcaption,.caption{color:#64748b;font-size:13px}pre{background:#f1f5f9;padding:16px;border:1px solid #d9d9d9;overflow:auto;font:13px/1.5 Consolas,monospace}
.pagebreak{margin-top:35px}@media print{.pagebreak{break-before:page}body{font-size:11px}pre{font-size:8px}table{font-size:10px}}'''
(OUT/'report.html').write_text(('<!doctype html><html lang="ru"><meta charset="utf-8"><title>Лабораторная работа 1</title><style>'+css+'</style>'+''.join(html)+'</html>').replace('<link href=','<a href=').replace('</link>','</a>'),encoding='utf-8')
print('Created',OUT/'report.pdf')
print('Created',OUT/'report.html')
