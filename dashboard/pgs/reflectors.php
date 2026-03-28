<?php

$Result = @fopen($CallingHome['ServerURL']."?do=GetReflectorList", "r");

if (!$Result) die("HEUTE GIBTS KEIN BROT");

$INPUT = "";
while (!feof ($Result)) {
    $INPUT .= fgets ($Result, 1024);
}
fclose($Result);

$XML = new ParseXML();
$Reflectorlist = $XML->GetElement($INPUT, "reflectorlist");
$Reflectors    = $XML->GetAllElements($Reflectorlist, "reflector");

?>

<div style="margin-bottom: 12px; display: flex; align-items: center; gap: 12px; flex-wrap: wrap;">
   <input type="text" id="reflector-search" class="form-control" placeholder="Search reflector, country or comment..." style="max-width: 400px;">
   <span id="reflector-count" style="color: var(--text-muted); font-size: 13px;"></span>
</div>

<table class="table table-striped table-hover" id="reflector-table">
   <thead>
   <tr class="table-center">
      <th class="col-md-1">#</th>
      <th class="col-md-3">Reflector</th>
      <th class="col-md-3">Country</th>
      <th class="col-md-1">Service</th>
      <th class="col-md-4">Comment</th>
   </tr>
   </thead>
   <tbody>
<?php

for ($i=0;$i<count($Reflectors);$i++) {

   $NAME          = $XML->GetElement($Reflectors[$i], "name");
   $COUNTRY       = $XML->GetElement($Reflectors[$i], "country");
   $LASTCONTACT   = $XML->GetElement($Reflectors[$i], "lastcontact");
   $COMMENT       = $XML->GetElement($Reflectors[$i], "comment");
   $DASHBOARDURL  = $XML->GetElement($Reflectors[$i], "dashboardurl");

   echo '
 <tr class="table-center">
   <td class="row-num">'.($i+1).'</td>
   <td><a href="'.$DASHBOARDURL.'" target="_blank" class="listinglink" title="Visit the Dashboard of&nbsp;'.$NAME.'">'.$NAME.'</a></td>
   <td>'.$COUNTRY.'</td>
   <td><span class="status-dot '; if ($LASTCONTACT<(time()-600)) { echo 'status-down'; } ELSE { echo 'status-up'; } echo '"></span></td>
   <td>'.$COMMENT.'</td>
 </tr>';
}

?>
   </tbody>
</table>

<div id="reflector-pagination" style="text-align: center;"></div>

<script>
(function() {
   var perPage = 25;
   var currentPage = 1;
   var $table = $('#reflector-table');
   var $rows = $table.find('tbody tr');
   var $search = $('#reflector-search');
   var $pagination = $('#reflector-pagination');
   var $count = $('#reflector-count');
   var filtered = [];

   function getFilteredRows() {
      var q = $search.val().toLowerCase();
      if (!q) return $rows.toArray();
      return $rows.filter(function() {
         return $(this).text().toLowerCase().indexOf(q) > -1;
      }).toArray();
   }

   function renderPage() {
      filtered = getFilteredRows();
      var total = filtered.length;
      var pages = Math.ceil(total / perPage);
      if (currentPage > pages) currentPage = pages || 1;

      $rows.hide();
      var start = (currentPage - 1) * perPage;
      $(filtered.slice(start, start + perPage)).show().each(function(i) {
         $(this).find('.row-num').text(start + i + 1);
      });

      $count.text(total + ' reflector' + (total !== 1 ? 's' : '') + (pages > 1 ? ' — page ' + currentPage + '/' + pages : ''));

      // pagination
      if (pages <= 1) { $pagination.empty(); return; }
      var html = '<ul class="pagination">';
      html += '<li' + (currentPage === 1 ? ' class="disabled"' : '') + '><a href="#" data-page="' + (currentPage - 1) + '">&laquo;</a></li>';
      for (var p = 1; p <= pages; p++) {
         if (p === 1 || p === pages || (p >= currentPage - 1 && p <= currentPage + 1)) {
            html += '<li' + (p === currentPage ? ' class="active"' : '') + '><a href="#" data-page="' + p + '">' + p + '</a></li>';
         } else if (p === currentPage - 2 || p === currentPage + 2) {
            html += '<li class="disabled"><span>&hellip;</span></li>';
         }
      }
      html += '<li' + (currentPage === pages ? ' class="disabled"' : '') + '><a href="#" data-page="' + (currentPage + 1) + '">&raquo;</a></li>';
      html += '</ul>';
      $pagination.html(html);
   }

   $search.on('keyup', function() {
      currentPage = 1;
      renderPage();
   });

   $pagination.on('click', 'a', function(e) {
      e.preventDefault();
      var p = parseInt($(this).data('page'));
      if (p < 1 || p > Math.ceil(filtered.length / perPage)) return;
      currentPage = p;
      renderPage();
   });

   renderPage();
})();
</script>

