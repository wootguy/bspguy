reduce font size with this site:
https://www.fontsquirrel.com/tools/webfont-generator

create header code with this command:
python -c "import sys;a=sys.argv;open(a[2],'wb').write(('const unsigned char '+a[3]+'[] = {'+','.join([hex(b) for b in open(a[1],'rb').read()])+'};').encode('utf-8'))" fontname.ttf fontname.h fontname
