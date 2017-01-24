#!/usr/bin/bash
filename="$1"
i=1
while read -r line
do
  query="$line"
  echo "Query read from file - $query"
  
  echo "$query" > q${i}_1.txt
  
  for n in {1..10}; do
    echo "$query" >> q${i}_10.txt
  done

  for n in {1..100}; do
    echo "$query" >> q${i}_100.txt
  done

  for n in {1..1000}; do
    echo "$query" >> q${i}_1000.txt
  done
  i=$((i+1))
done < "$filename"
