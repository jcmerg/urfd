<?php
// QuadNet Live - parsed natively from OpenQuad ICS data stream
?>
<h2 class="sub-header">QuadNet Live</h2>
<div style="margin-bottom: 12px; display: flex; align-items: center; gap: 12px; flex-wrap: wrap;">
   <input type="text" id="quadnet-search" class="form-control" placeholder="Search callsign, name or reflector..." style="max-width: 400px;">
   <span id="quadnet-status" style="color: var(--text-muted); font-size: 13px;"></span>
</div>

<table class="table table-striped table-hover table-condensed" id="quadnet-table">
   <thead>
   <tr class="table-center">
      <th>Time</th>
      <th>Callsign</th>
      <th>Radio</th>
      <th>Target</th>
      <th>Repeater</th>
      <th class="mobile-hide">Name</th>
      <th>Reflector</th>
      <th class="mobile-hide">Duration</th>
   </tr>
   </thead>
   <tbody id="quadnet-body">
   <tr><td colspan="8" style="text-align:center; color: var(--text-muted);">Loading...</td></tr>
   </tbody>
</table>

<script>
(function() {
   var $body = $('#quadnet-body');
   var $search = $('#quadnet-search');
   var $status = $('#quadnet-status');
   var entries = [];

   function cleanCall(s) {
      // Preserve portable/mobile suffixes: _P -> /P, _M -> /M, _AM -> /AM etc.
      var m = s.match(/^(.+?)_(P|M|AM|MM|QRP)$/);
      if (m) return m[1].replace(/_/g, '') + '/' + m[2];
      return s.replace(/_/g, '');
   }

   function cleanModule(s) {
      // e.g. W2ECR__B -> W2ECR-B, DCS237_A -> DCS237-A
      return s.replace(/_/g, '').replace(/([A-Z0-9])([A-Z])$/, '$1-$2');
   }

   function cleanName(s) {
      return s.replace(/_/g, ' ').trim();
   }

   function parseLine(line) {
      var m = line.match(/^(\d{4}-\d{2}-\d{2})\s+(\d{2}:\d{2}:\d{2})\s+([\d.]+)s:\s*(\d+)%:\s*([\d.]+)%\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(.*?)\s+(\S+)\s*$/);
      if (!m) return null;
      var cr = m[6].split('/');
      return {
         time: m[2],
         duration: m[3] + 's',
         call: cleanCall(cr[0] || ''),
         radio: (cr[1] || '').replace(/_/g, ''),
         target: cleanCall(m[7]),
         repeater: cleanModule(m[8]),
         name: cleanName(m[10]),
         reflector: cleanModule(m[11])
      };
   }

   function render() {
      var q = $search.val().toLowerCase();
      var html = '';
      var shown = 0;
      for (var i = entries.length - 1; i >= 0; i--) {
         var e = entries[i];
         if (q) {
            var text = [e.call, e.radio, e.target, e.repeater, e.name, e.reflector].join(' ').toLowerCase();
            if (text.indexOf(q) === -1) continue;
         }
         html += '<tr class="table-center">' +
            '<td>' + e.time + '</td>' +
            '<td><strong>' + e.call + '</strong></td>' +
            '<td>' + e.radio + '</td>' +
            '<td>' + e.target + '</td>' +
            '<td>' + e.repeater + '</td>' +
            '<td class="mobile-hide">' + e.name + '</td>' +
            '<td>' + e.reflector + '</td>' +
            '<td class="mobile-hide">' + e.duration + '</td>' +
            '</tr>';
         shown++;
      }
      if (!html) html = '<tr><td colspan="8" style="text-align:center; color: var(--text-muted);">No data</td></tr>';
      $body.html(html);
      $status.text(shown + ' entries');
   }

   function poll() {
      $.get('./pgs/quadnet_proxy.php', function(data) {
         var lines = data.split('\n');
         var parsed = [];
         for (var i = 0; i < lines.length; i++) {
            var e = parseLine(lines[i]);
            if (e) parsed.push(e);
         }
         entries = parsed;
         render();
      }).always(function() {
         setTimeout(poll, 3000);
      });
   }

   $search.on('keyup', function() { render(); });
   poll();
})();
</script>
