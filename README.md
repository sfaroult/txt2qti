# txt2qti
Converts .txt quizzes to QTI format

Quick and dirty utility initially written to batch upload quiz questions into Canvas.

Text quizzes are expected to be in a kind of Respondus or Aiken format,
eg something such as:

<pre>
Which English King didn't appear in the title of a Shakespeare play:
*1) Henry III
2) Henry IV
3) Henry V
4) Henry VI
*5) Henry VII
6) Henry VIII
</pre>

(above is a Respondus format)

<pre>
Which English King didn't appear in the title of a Shakespeare play:
a. Henry III
b. Henry IV
c. Henry V
d. Henry VI
e. Henry VII
f. Henry VIII
answer: a, e
</pre>

(above is an extended Aiken format, that normally supports only one answer).

Simply put, the program tries to make sense of what it reads. Anything fancy
(feedback, etc) is completely ignored in this raw bones version (by 
"ignored" I don't mean "skipped", I mean misunderstood).

Generally speaking, multiple blocks of questions/answers in a file are
separated by empty lines. However, empty lines are ignored in question text
(NOT in answers) in two cases:
<ul>
<li>When appearing between &lt;pre&gt; &lt;/pre&gt; tags that mark a block of code.</li>
<li>When appearing between &lt;block&gt; &lt;/block&gt; tags.</li>
</ul>

The first set of tag is kept into the QTI file that is generated, and passed
on to Canvas. HTML entities are escaped in a block of code, which avoids
seeing #include &lt;stdio.h&gt; turn into a mysterious #include (a problem I have
had even editing interactively in Canvas).

The &lt;block&gt;&lt;/block&gt; set of tags is for the only benefit of txt2qti and stripped.

Note that tags are processed through very basic string processing and must
appear as shown above (no spaces in tags).

The program was developed on Mac OS X and should normally compile on Ubuntu.
Much less certain about operating systems I only touch with a barge pole.

Usage:
<pre>
./txt2qti [-t title] textfile [textfile ...]
</pre>
It generates a file named title.zip or Quiz_<timestamp>.zip if no title was
provided.


Usual claims about using at your own risk.
