<?xml version="1.0" encoding="Shift_JIS"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/TR/WD-xsl" xml:lang="ja">
<xsl:template match="/">
	<html lang="ja">
	<head>
		<title>hoot タイトルリスト(<xsl:value-of select="gamelist/@date"/>版)</title>
		<meta http-equiv="Content-Type" content="text/html; charset=Shift_JIS"/>
		<link rel="stylesheet" type="text/css" href="hoot.css"/>
		<script language="JavaScript">
		<xsl:comment>
		<![CDATA[
		var doc = document.XMLDocument;
		var style = document.XSLDocument;
		var sortfield = style.selectSingleNode("//@order-by");
		function sort_list(f)
		{
			sortfield.value = f;
			gamelist.innerHTML = doc.documentElement.transformNode(style);
		}
		]]>
		</xsl:comment>
		</script>
	</head>
	<body>
		<h1>hoot タイトルリスト(<xsl:value-of select="gamelist/@date"/>版)</h1>
		<button onClick="sort_list('dummy')">ソートなし</button>
		<button onClick="sort_list('driver; driver/@type; name')">ドライバでソート</button>
		<button onClick="sort_list('name; driver; driver/@type')">タイトルでソート</button>
		<div id="gamelist">
		<xsl:apply-templates select="gamelist"/>
		</div>
	</body>
	</html>
</xsl:template>
<xsl:template match="gamelist">
	<xsl:for-each select="game" order-by="dummy">
		<hr/>
		<h2><xsl:value-of select="name"/></h2>
		<h3>
			ドライバ:<xsl:value-of select="driver"/>
			(<xsl:value-of select="driver/@type"/>)
		</h3>
		<xsl:apply-templates select="options"/>
		<xsl:apply-templates select="romlist"/>
		<xsl:apply-templates select="titlelist"/>
	</xsl:for-each>
</xsl:template>
<xsl:template match="romlist">
	<h4>データ</h4>
	<table>
	<tr>
		<th width="10%">種類</th>
		<th width="15%">オフセット</th>
		<th>ファイル名</th>
	</tr>
	<xsl:for-each select="rom">
		<tr>
			<td><xsl:value-of select="@type"/></td>
			<td><xsl:value-of select="@offset"/></td>
			<td><xsl:value-of/></td>
		</tr>
	</xsl:for-each>
	</table>
</xsl:template>
<xsl:template match="options">
	<h4>オプション</h4>
	<table>
	<tr>
		<th width="25%">名前</th>
		<th>値</th>
	</tr>
	<xsl:for-each select="option">
		<tr>
			<td><xsl:value-of select="@name"/></td>
			<td><xsl:value-of select="@value"/></td>
		</tr>
	</xsl:for-each>
	</table>
</xsl:template>
<xsl:template match="titlelist">
	<h4>曲名</h4>
	<table>
	<tr>
		<th width="10%">No.</th>
		<th>タイトル</th>
	</tr>
	<xsl:for-each select="title">
		<tr>
			<td><xsl:value-of select="@code"/></td>
			<td><xsl:value-of/></td>
		</tr>
	</xsl:for-each>
	</table>
</xsl:template>
</xsl:stylesheet>
