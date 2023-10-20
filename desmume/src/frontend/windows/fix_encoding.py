import chardet
import codecs

def conv_encoding(filename_in):
	encoding_type = ''
	with open(filename_in, 'rb') as f:
		data = f.read()
		encoding_type = chardet.detect(data)
		f.close()

	encode_in = encoding_type['encoding']
	if encode_in == 'utf-8':
		print(filename_in + ': Already UTF-8!')
		return
	encode_out = 'utf-8'

	with codecs.open(filename=filename_in, mode='r', encoding=encode_in) as fi:
		data = fi.readlines()
		fi.close()
		with codecs.open(filename_in, mode='w', encoding=encode_out) as fo:
			for line in data:
				if '#pragma code_page(' in line:
					line = '#pragma code_page(65001)\n'
				fo.write(line)
			fo.close()
	print('Fix encoding for ' + filename_in + ' done!')

if __name__ == "__main__":
	res_h = './resource.h'
	res_rc = './resources.rc'
	try:
		conv_encoding(res_h)
		conv_encoding(res_rc)
	except:
		pass
