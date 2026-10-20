"""Independent checks of recorded experiments and the high-precision reference."""
from pathlib import Path
from decimal import Decimal as D, getcontext
from statistics import median
import csv, json, math, subprocess

ROOT = Path(__file__).resolve().parents[1]
getcontext().prec = 60
def primitive(x):
    t, r = (x + 2).sqrt(), D(2).sqrt()
    return D(35)/(32*r)*((t-r)/(t+r)).ln() + t*(D(3)/(16*x)-D(1)/(4*x*x))
reference = primitive(D(5)) - primitive(D(2))
# Independent composite Simpson rule in Decimal arithmetic.
n = 16384
h = D(3)/n
def f(x): return (1+x*x)/(x*x*x*(2+x).sqrt())
simpson = h/3 * (f(D(2))+f(D(5)) + sum(
    (4 if i%2 else 2)*f(D(2)+i*h) for i in range(1,n)))
assert abs(reference-simpson) < D('1e-16')

def read(name):
    with (ROOT/'results'/name).open(encoding='utf-8-sig') as stream:
        return list(csv.DictReader(stream))
raw, summary, accuracy = read('raw.csv'), read('summary.csv'), read('accuracy.csv')
meta = dict(line.split('=',1) for line in (ROOT/'results/metadata.txt').read_text().splitlines())
N, pmax, repeats = int(meta['N']), int(meta['max_threads']), int(meta['repeats'])
assert len(raw) == repeats*(2*pmax+1)
assert len(summary) == 2*pmax
for row in summary:
    tech, p = row['technology'], int(row['threads'])
    runs = [float(r['seconds']) for r in raw if r['technology']==tech and int(r['threads'])==p]
    assert len(runs) == repeats and min(runs) > 0
    assert float(row['seconds']) == median(runs)
    base = next(float(r['seconds']) for r in summary if r['technology']==tech and r['threads']=='1')
    speed = base/float(row['seconds'])
    assert math.isclose(float(row['speedup']),speed,rel_tol=1e-14)
    assert math.isclose(float(row['efficiency_percent']),100*speed/p,rel_tol=1e-14)
    assert abs(D(row['result'])-reference) < D('1.16015625')/N
values = [float(r['result']) for r in raw]
assert max(values)-min(values) < 2e-14
for row in accuracy:
    n = int(row['n'])
    error = abs(D(row['result'])-reference)
    assert error <= D(row['bound'])
    assert math.isclose(float(row['absolute_error']),float(error),rel_tol=1e-7,abs_tol=1e-15)
program = ROOT/'x64/Release/L1pp.exe'
checks = []
for args in [('0','1','1'),('10','0','2'),('10','11','2'),('10','1','11'),
             ('10','1','257'),('-1','1','1'),('abc','1','1'),('10','1','2','2'),
             ('18446744073709551616','1','1')]:
    result = subprocess.run([str(program),*args],capture_output=True)
    assert result.returncode != 0, args
    checks.append(' '.join(args))
out = {'reference_decimal_60_digits':str(reference), 'independent_simpson':str(simpson),
       'reference_check_difference':str(abs(reference-simpson)),
       'max_parallel_result_spread':max(values)-min(values),
       'validated_raw_rows':len(raw),'validated_summary_rows':len(summary),
       'invalid_cli_cases':checks,'status':'PASS'}
(ROOT/'results/verification.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(out,ensure_ascii=False,indent=2))
