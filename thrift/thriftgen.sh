thrift -gen cpp -out . netplay.thrift
rm -rf *skeleton*

FILES=./*.cpp
for f in $FILES
do
  filename=$(basename "$f")
  filename="${filename%.*}"
  mv $f "${filename}.cc"
done

mkdir include
mv *.h include
