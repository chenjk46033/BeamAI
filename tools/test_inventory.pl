#!/usr/bin/env perl
# Builds docs/unit-tests.md from a gtest XML result file.
#
# The XML is authoritative for name, file, line and outcome. The source is read
# only for the purpose line (the comment above each TEST) and to classify the
# test as unit or integration.
use strict;
use warnings;

my $xml_path = shift @ARGV or die "usage: test_inventory.pl <gtest.xml> <out.md> <repo-root>\n";
my $out_path = shift @ARGV or die "need an output path\n";
my $root = shift @ARGV or die "need the repo root\n";
my $html_path = shift @ARGV;  # optional: also write a browser-readable page

open(my $in, '<', $xml_path) or die "open $xml_path: $!";
my $xml = do { local $/; <$in> };
close $in;

my ($run_stamp) = $xml =~ m{<testsuites\b[^>]*\btimestamp="([^"]+)"};
$run_stamp = 'unknown' unless defined $run_stamp;
$run_stamp =~ s{T}{ };
$run_stamp =~ s{:\d\d\.\d+$}{};          # to the minute
my ($total) = $xml =~ m{<testsuites\b[^>]*\btests="(\d+)"};
my ($failed) = $xml =~ m{<testsuites\b[^>]*\bfailures="(\d+)"};
my ($disabled) = $xml =~ m{<testsuites\b[^>]*\bdisabled="(\d+)"};
my ($elapsed) = $xml =~ m{<testsuites\b[^>]*\btime="([\d.]+)"};

my $bs = chr(92);

# Source cache, so each test file is read once.
my %src;
sub lines_of {
    my $file = shift;
    return $src{$file} if exists $src{$file};
    my @l;
    if (open(my $fh, '<', $file)) {
        @l = <$fh>;
        close $fh;
    }
    $src{$file} = \@l;
    return $src{$file};
}

# The comment block immediately above a TEST, which is where this codebase
# states intent. Blank or absent means nobody wrote one.
sub purpose_at {
    my ($lines, $line_no) = @_;
    my @out;
    # Upwards from the TEST, across blank lines, taking the comment block.
    my $i = $line_no - 2;
    $i-- while $i >= 0 && defined $lines->[$i] && $lines->[$i] =~ m{^\s*$};
    while ($i >= 0) {
        my $t = $lines->[$i];
        last unless defined $t && $t =~ m{^\s*//};
        $t =~ s/\s+$//;
        $t =~ s{^\s*//\s?}{};
        unshift @out, $t;
        $i--;
    }
    return clean(join(' ', @out)) if @out;

    # Else the comment the test opens with, which is where several state intent.
    my $j = $line_no;
    while (defined $lines->[$j] && $lines->[$j] =~ m{^\s*//}) {
        my $t = $lines->[$j];
        $t =~ s/\s+$//;
        $t =~ s{^\s*//\s?}{};
        push @out, $t;
        $j++;
    }
    return clean(join(' ', @out)) if @out;
    return '';
}

# Pipes would split a markdown table cell, so they are escaped here rather
# than in clean(), which the HTML path also uses.
sub md {
    my $t = shift // '';
    $t =~ s/\|/ chr(92) . '|' /ge;
    return $t;
}

sub clean {
    my $text = shift // '';
    $text =~ s/\s+/ /g;
    $text =~ s/^\s+|\s+$//g;
    return $text;
}

# Last resort: the test's own name. These are written as sentences already --
# IsppaOverLimitFailsAndSkipsIsptaBlock -- so they carry the intent.
sub from_name {
    my ($suite, $name) = @_;
    my $words = sub {
        my $s = shift;
        $s =~ s/([a-z0-9])([A-Z])/$1 $2/g;
        $s =~ s/([A-Z]+)([A-Z][a-z])/$1 $2/g;
        $s =~ s/_/ /g;
        my @w = split /\s+/, $s;
        for my $k (0 .. $#w) {
            next if $w[$k] =~ /^[A-Z0-9]{2,}$/;      # keep MRI, CSV, ISPTA
            $w[$k] = lc $w[$k] unless $k == 0;
        }
        return join(' ', @w);
    };
    my $head = $words->($suite);
    my $tail = $words->($name);
    $tail = lcfirst $tail unless $tail =~ /^[A-Z0-9]{2,}/;
    return clean("$head: $tail");
}

# Anything that reaches outside the process -- a file, a real scan, a store --
# makes it an integration test here. Everything else is pure logic.
my @integration_markers = (
    qr{BEAM_[A-Z0-9_]*_(?:MAT|CSV|NII|DICOM|DIR|PATH)},
    qr{std::filesystem},
    qr{QTemporaryDir}, qr{QTemporaryFile}, qr{QDir\b}, qr{QFile\b},
    qr{std::ifstream}, qr{std::ofstream},
    qr{\.load\(}, qr{\.save\(},
    qr{readTreatmentSession}, qr{writeTreatmentSession},
    qr{loadLegacyBeam}, qr{loadMriRas}, qr{readNifti}, qr{loadDicom},
);

sub body_at {
    my ($lines, $line_no) = @_;
    my ($depth, $started, $text) = (0, 0, '');
    for (my $i = $line_no - 1; $i < scalar(@$lines); $i++) {
        my $t = $lines->[$i] // last;
        $text .= $t;
        my $opens = () = $t =~ /\{/g;
        my $closes = () = $t =~ /\}/g;
        $depth += $opens - $closes;
        $started = 1 if $opens;
        last if $started && $depth <= 0;
    }
    return $text;
}

sub classify {
    my $body = shift;
    for my $m (@integration_markers) {
        return 'integration' if $body =~ $m;
    }
    return 'unit';
}

# libs/<name> when the test file names one, else the file stem.
sub module_of {
    my $file = shift;
    my @parts = split(/\Q$bs\E|\//, $file);
    my $stem = $parts[-1];
    $stem =~ s/_tests?\.cpp$//;
    $stem =~ s/\.cpp$//;
    return "libs/$stem" if -d "$root/libs/$stem";
    return $stem;
}

my @tests;
while ($xml =~ m{<testcase\b([^>]*?)(/>|>(.*?)</testcase>)}gs) {
    my ($attrs, $tail, $inner) = ($1, $2, $3 // '');
    my %a;
    while ($attrs =~ m{(\w+)="([^"]*)"}g) { $a{$1} = $2; }
    next unless $a{name} && $a{classname};

    my $file = $a{file} // '';
    my $line = $a{line} // 0;
    my $lines = $file ? lines_of($file) : [];

    my $rel = $file;
    my $rootfs = $root;
    $rel =~ s{\Q$bs\E}{/}g;
    $rootfs =~ s{\Q$bs\E}{/}g;
    $rel =~ s{^\Q$rootfs\E/?}{}i;

    my $result = 'pass';
    $result = 'FAIL' if $inner =~ m{<failure};
    $result = 'skipped' if ($a{result} // '') eq 'skipped';
    $result = 'disabled' if ($a{status} // '') eq 'notrun';

    push @tests, {
        suite => $a{classname},
        name => $a{name},
        path => $line ? "$rel:$line" : $rel,
        purpose => (@$lines ? purpose_at($lines, $line) : ''),
        type => @$lines ? classify(body_at($lines, $line)) : 'unit',
        module => module_of($file),
        result => $result,
        time => $a{time} // '',
    };
    # Nothing written anywhere: read it off the name, and mark it as derived so
    # the document does not present a guess as documentation.
    if ($tests[-1]{purpose} eq '') {
        $tests[-1]{purpose} = from_name($tests[-1]{suite}, $tests[-1]{name});
        $tests[-1]{derived} = 1;
    }
}

die "no testcases parsed from $xml_path\n" unless @tests;

my %by_module;
push @{ $by_module{ $_->{module} } }, $_ for @tests;

my ($units, $integrations) = (0, 0);
for my $t (@tests) { $t->{type} eq 'unit' ? $units++ : $integrations++; }

open(my $out, '>', $out_path) or die "open $out_path: $!";
print $out "# Unit tests\n\n";
print $out "Generated by `tools/test_inventory.sh` from the gtest run below.\n";
print $out "Do not edit by hand.\n\n";
print $out "| | |\n| --- | --- |\n";
printf $out "| Run at | %s |\n", $run_stamp;
printf $out "| Tests | %s |\n", $total // scalar(@tests);
printf $out "| Failures | %s |\n", $failed // 0;
printf $out "| Disabled | %s |\n", $disabled // 0;
printf $out "| Elapsed | %s s |\n", $elapsed // '?';
printf $out "| Unit / integration | %d / %d |\n", $units, $integrations;
printf $out "| Modules | %d |\n\n", scalar(keys %by_module);

print $out "`integration` marks a test that reaches a file, a real scan or a\n";
print $out "store; `unit` is pure in-process logic. Purpose is the comment above\n";
print $out "the test in its source, so a blank one means none was written.\n\n";

for my $m (sort keys %by_module) {
    my @rows = sort { $a->{suite} cmp $b->{suite} || $a->{name} cmp $b->{name} }
               @{ $by_module{$m} };
    my $bad = grep { $_->{result} eq 'FAIL' } @rows;
    printf $out "## %s\n\n%d test%s%s\n\n", $m, scalar(@rows),
        (scalar(@rows) == 1 ? '' : 's'), ($bad ? " — **$bad failing**" : '');
    print $out "| Test | Path | Purpose | From | Type | Result |\n";
    print $out "| --- | --- | --- | --- | --- | --- |\n";
    for my $t (@rows) {
        printf $out "| %s.%s | `%s` | %s | %s | %s | %s |\n",
            $t->{suite}, $t->{name}, $t->{path}, md($t->{purpose}),
            ($t->{derived} ? 'name' : 'comment'),
            $t->{type}, $t->{result};
    }
    print $out "\n";
}
close $out;

# ---- html, for reading in a browser ----
if (defined $html_path) {
    sub esc {
        my $s = shift // '';
        $s =~ s/&/&amp;/g; $s =~ s/</&lt;/g; $s =~ s/>/&gt;/g;
        return $s;
    }

    my $sections = '';
    for my $m (sort keys %by_module) {
        my @rows = sort { $a->{suite} cmp $b->{suite} || $a->{name} cmp $b->{name} }
                   @{ $by_module{$m} };
        my $bad = grep { $_->{result} eq 'FAIL' } @rows;
        $sections .= sprintf(
            '<section><h2>%s <span class="count">%d test%s%s</span></h2><div class="wrap"><table>'
            . '<colgroup><col style="width:22%%"><col style="width:38%%"><col style="width:8%%">'
            . '<col style="width:18%%"><col style="width:7%%"><col style="width:7%%"></colgroup>'
            . '<thead><tr><th>test</th><th>purpose</th><th>from</th><th>path</th><th>type</th><th>result</th>'
            . '</tr></thead><tbody>' . "\n",
            esc($m), scalar(@rows), (scalar(@rows) == 1 ? '' : 's'),
            ($bad ? sprintf(' &middot; <b class="bad">%d failing</b>', $bad) : ''));
        for my $t (@rows) {
            # A few purposes are whole paragraphs; the cell shows the opening
            # and carries the rest in its tooltip.
            my $full = $t->{purpose};
            my $shown = length($full) > 260 ? substr($full, 0, 257) . '...' : $full;
            my $from = $t->{derived} ? 'name' : 'comment';
            $sections .= sprintf(
                '<tr><td class="name"><b>%s</b>.%s</td><td class="purpose" title="%s">%s</td>'
                . '<td><span class="tag %s">%s</span></td>'
                . '<td class="path">%s</td><td><span class="tag %s">%s</span></td>'
                . '<td><span class="tag %s">%s</span></td></tr>' . "\n",
                esc($t->{suite}), esc($t->{name}),
                esc($full), esc($shown),
                $from, $from,
                esc($t->{path}),
                $t->{type}, $t->{type},
                ($t->{result} eq 'pass' ? 'pass' : ($t->{result} eq 'FAIL' ? 'bad' : 'other')),
                esc($t->{result}));
        }
        $sections .= "</tbody></table></div></section>\n";
    }

    my $html = <<"HTML";
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Beam unit tests</title>
<style>
  :root {
    --bg:#f7fafb; --panel:#fff; --ink:#17313f; --dim:#4f6b78; --line:#dde6ea;
    --accent:#167e91; --pass:#2e9b7a; --bad:#d4564f; --other:#d8a12a;
  }
  \@media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --bg:#10242c; --panel:#16303a; --ink:#e8f1f4; --dim:#a9c2cd; --line:#23424e;
      --accent:#5ccde5; --pass:#49b994; --bad:#e0736c; --other:#e3b556;
    }
  }
  * { box-sizing:border-box; }
  body { margin:0; padding:30px 18px 70px; background:var(--bg); color:var(--ink);
         font:16px/1.55 "Segoe UI",system-ui,sans-serif; }
  main { max-width:1180px; margin:0 auto; }
  h1 { font-size:15px; letter-spacing:.09em; text-transform:uppercase;
       color:var(--dim); margin:0 0 18px; font-weight:600; }
  .head { background:var(--panel); border:1px solid var(--line); border-radius:10px;
          padding:20px 22px; margin-bottom:14px; display:flex; gap:28px; flex-wrap:wrap; }
  .stat b { display:block; font-size:26px; font-variant-numeric:tabular-nums;
            color:var(--accent); line-height:1.1; }
  .stat span { font-size:12.5px; color:var(--dim); letter-spacing:.05em;
               text-transform:uppercase; }
  #q { width:100%; padding:10px 13px; margin-bottom:22px; border-radius:8px;
       border:1px solid var(--line); background:var(--panel); color:var(--ink); font-size:14px; }
  #q:focus { outline:none; border-color:var(--accent); }
  section { margin-bottom:26px; }
  h2 { font-size:15px; margin:0 0 8px; }
  .count { font-weight:400; font-size:14px; color:var(--dim); }
  .wrap { background:var(--panel); border:1px solid var(--line); border-radius:10px;
          overflow-x:auto; }
  table { width:100%; border-collapse:collapse; font-size:15px; table-layout:fixed; }
  th { text-align:left; font-size:12.5px; letter-spacing:.07em; text-transform:uppercase;
       color:var(--dim); padding:10px 13px; border-bottom:1px solid var(--line); }
  td { padding:8px 13px; border-bottom:1px solid var(--line); vertical-align:top; }
  tr:last-child td { border-bottom:none; }
  .name { word-break:break-word; }
  .purpose { color:var(--ink); }
  .tag.name { color:var(--other); border-color:var(--other); }
  .tag.comment { color:var(--dim); }
  .path { color:var(--dim); font-family:Consolas,monospace; font-size:13.5px; word-break:break-all; }
  .tag { display:inline-block; padding:2px 9px; border-radius:10px; font-size:13px;
         border:1px solid var(--line); white-space:nowrap; }
  .tag.pass { color:var(--pass); border-color:var(--pass); }
  .tag.bad, .bad { color:var(--bad); border-color:var(--bad); }
  .tag.other { color:var(--other); border-color:var(--other); }
  .tag.integration { color:var(--accent); border-color:var(--accent); }
  .tag.unit { color:var(--dim); }
  footer { color:var(--dim); font-size:14px; margin-top:20px; }
</style>
</head>
<body>
<main>
  <h1>Beam unit tests</h1>
  <div class="head">
    <div class="stat"><b>$run_stamp</b><span>run at</span></div>
    <div class="stat"><b>@{[ $total // scalar(@tests) ]}</b><span>tests</span></div>
    <div class="stat"><b>@{[ $failed // 0 ]}</b><span>failures</span></div>
    <div class="stat"><b>$units / $integrations</b><span>unit / integration</span></div>
    <div class="stat"><b>@{[ scalar(keys %by_module) ]}</b><span>modules</span></div>
    <div class="stat"><b>@{[ $elapsed // '?' ]}s</b><span>elapsed</span></div>
  </div>
  <input id="q" type="search" placeholder="Filter by test, purpose, path, type or result...">
$sections  <footer>Name, path and result come from gtest's own XML. Purpose is the comment
    above or inside each test when <b>from</b> says comment, and read from the test's
    own name when it says name. <b>integration</b> marks a test that reaches a file, a real scan or a store.</footer>
</main>
<script>
  const q = document.getElementById('q');
  q.addEventListener('input', () => {
    const needle = q.value.toLowerCase();
    for (const section of document.querySelectorAll('section')) {
      let shown = 0;
      for (const row of section.querySelectorAll('tbody tr')) {
        const hit = row.textContent.toLowerCase().includes(needle);
        row.hidden = !hit;
        if (hit) shown++;
      }
      section.hidden = shown === 0;
    }
  });
</script>
</body>
</html>
HTML

    open(my $h, '>', $html_path) or die "open $html_path: $!";
    print $h $html;
    close $h;
    print "  wrote $html_path\n";
}

printf "  %d tests, %d modules, %d unit / %d integration\n",
    scalar(@tests), scalar(keys %by_module), $units, $integrations;
printf "  run at %s, %s failures\n", $run_stamp, $failed // 0;
printf "  wrote %s\n", $out_path;
